/*
 * Copyright (c) 2026-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor any of its contributors may be used to
 *    endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */

#include "osTest.h"
#include "It_los_hpl.h"
#include "los_hpl_logger_pri.h"

/* 用例简要描述: 全类型参数字节流结构校验——逐格式断言运行时类型编码与压缩字节布局（%d %x %X %u %c %s %f %lf %p %ld %lu %lx %lld %llu %llx 组合、动态宽度精度 %*d %*s %.*f %*.*d、静态宽度精度 flag %5.2f %-10s %#x %010d % d、%% 与空格式、不支持格式已知丢参行为、28/29 参上限、700/727/750 长串边界与自定义串条目布局） */

#define HPL_PROBE_ROW_MAX       48
#define HPL_PROBE_LONG_STR_700  700
#define HPL_PROBE_LONG_STR_727  727
#define HPL_PROBE_LONG_STR_750  750
#define HPL_PROBE_CUSTOM_STR    "hpl015 custom"
#define HPL_PROBE_ERR_NONE      0xFFFFFFFF

/* HplCustomStringHeader size: magic + [timestamp when lock-free] + dataLength. */
#ifdef LOSCFG_KERNEL_HPL_TASK_LOCK_FREE
#define HPL_PROBE_CUSTOM_HDR_SIZE  16
#else
#define HPL_PROBE_CUSTOM_HDR_SIZE  8
#endif

typedef struct {
    UINT32 fmtId;        /* fmtId assigned by the runtime preprocessor. */
    UINT32 wantEntries;  /* expected number of compressed args entries. */
    UINT32 wantPayload;  /* expected args payload bytes of the first entry. */
    const VOID *content; /* expected payload content, NULL skips memcmp. */
    UINT32 contentLen;   /* memcmp length. */
} HplProbeRow;

typedef struct {
    UINT32 entryCnt;
    UINT32 payloadLen;
    const UINT8 *payload;
} HplProbeSeen;

STATIC HplProbeRow g_probeRows[HPL_PROBE_ROW_MAX];
STATIC HplProbeSeen g_probeSeen[HPL_PROBE_ROW_MAX];
STATIC UINT64 g_probeAna[HPL_PROBE_ROW_MAX];
STATIC UINT32 g_probeRowCnt;
STATIC UINT32 g_probeLast;
/* First encoding-expectation violation, recorded by the worker; the test task
 * asserts it after the worker finished (ICUNIT asserts cannot return from the
 * worker without killing the suite). kind: 1=argNum 2=argType 3=analysis
 * 4=rejected-fmtId 5=accepted-fmtId. */
STATIC UINT32 g_probeErrRow = HPL_PROBE_ERR_NONE;
STATIC UINT32 g_probeErrKind;

/* Mirrors the HP_LOG expansion (los_hpl.h:197-206) but keeps the per-site
 * fmtId/analysis accessible: the case asserts the runtime analysis result
 * right after the call and correlates the compressed stream entries by
 * fmtId after the drain. Each macro expansion is its own call site, i.e.
 * its own STATIC fmtId, exactly like a distinct HP_LOG source line. */
#define HPL_PROBE_CALL(formatStr, ...)                                        \
    do {                                                                      \
        STATIC UINT32 probeFmtId = LOS_HPL_UNASSIGNED_LOGID;                   \
        STATIC UINT64 probeAnalysis = 0;                                      \
        OsHplStaticInfo probeFileInfo = {                                     \
            .fileName = __FILE__,                                             \
            .funcName = __FUNCTION__,                                         \
            .line = __LINE__,                                                 \
        };                                                                    \
        OsHplLogger(&probeFmtId, &probeAnalysis, &probeFileInfo, HPL_DEBUG,   \
                    formatStr, ##__VA_ARGS__);                                \
        g_probeAna[g_probeRowCnt] = probeAnalysis;                            \
        g_probeRows[g_probeRowCnt].fmtId = probeFmtId;                        \
        g_probeRows[g_probeRowCnt].wantEntries = 0;                           \
        g_probeRows[g_probeRowCnt].wantPayload = 0;                           \
        g_probeRows[g_probeRowCnt].content = NULL;                            \
        g_probeRows[g_probeRowCnt].contentLen = 0;                            \
        g_probeLast = g_probeRowCnt;                                          \
        g_probeRowCnt++;                                                      \
    } while (0)

#define HPL_PROBE_ERR_RECORD(kind)                                            \
    do {                                                                      \
        if (g_probeErrRow == HPL_PROBE_ERR_NONE) {                            \
            g_probeErrRow = g_probeLast;                                      \
            g_probeErrKind = (UINT32)(kind);                                  \
        }                                                                     \
    } while (0)

/* Record the argument-count expectation violation of the last probe. */
#define HPL_PROBE_ASSERT_NUM(wantNum)                                         \
    do {                                                                      \
        if (HPL_GET_ARGS_NUM(g_probeAna[g_probeLast]) != (UINT32)(wantNum)) { \
            HPL_PROBE_ERR_RECORD(1);                                          \
        }                                                                     \
    } while (0)

/* Record the argument-type expectation violation of the last probe. */
#define HPL_PROBE_ASSERT_TYPE(argIdx, wantType)                               \
    do {                                                                      \
        if (HPL_NEXT_ARGS_TYPE(g_probeAna[g_probeLast], (argIdx)) !=          \
            (UINT64)(wantType)) {                                             \
            HPL_PROBE_ERR_RECORD(2);                                          \
        }                                                                     \
    } while (0)

/* Record the expectation that the last probe was rejected (29+ arguments). */
#define HPL_PROBE_ASSERT_REJECT()                                             \
    do {                                                                      \
        if (g_probeAna[g_probeLast] != LOS_HPL_INVALID_ANALYSIS) {            \
            HPL_PROBE_ERR_RECORD(3);                                          \
        } else if (g_probeRows[g_probeLast].fmtId != LOS_HPL_UNASSIGNED_LOGID) { \
            HPL_PROBE_ERR_RECORD(4);                                          \
        }                                                                     \
    } while (0)

/* Record the expectation that the last probe got a fmtId assigned. */
#define HPL_PROBE_ASSERT_ASSIGNED()                                           \
    do {                                                                      \
        if (g_probeRows[g_probeLast].fmtId == LOS_HPL_UNASSIGNED_LOGID) {     \
            HPL_PROBE_ERR_RECORD(5);                                          \
        }                                                                     \
    } while (0)

/* Record the expected compressed-stream outcome of the most recent probe.
 * Parameter names must differ from the HplProbeRow member names: the member
 * token after '.' would otherwise be substituted by the preprocessor. */
#define HPL_PROBE_STREAM(pEntries, pPayload, pContent, pContentLen)           \
    do {                                                                      \
        g_probeRows[g_probeLast].wantEntries = (UINT32)(pEntries);            \
        g_probeRows[g_probeLast].wantPayload = (UINT32)(pPayload);            \
        g_probeRows[g_probeLast].content = (pContent);                        \
        g_probeRows[g_probeLast].contentLen = (UINT32)(pContentLen);          \
    } while (0)

/* Long string fixtures: static so neither the worker nor the runner task
 * stack is consumed. */
STATIC CHAR g_str700[HPL_PROBE_LONG_STR_700 + 1];
STATIC CHAR g_str727[HPL_PROBE_LONG_STR_727 + 1];
STATIC CHAR g_str750[HPL_PROBE_LONG_STR_750 + 1];

STATIC UINT32 TestHplProbeLe32(const UINT8 *buf, UINT32 off)
{
    return (UINT32)buf[off] | ((UINT32)buf[off + 1] << 8) |
           ((UINT32)buf[off + 2] << 16) | ((UINT32)buf[off + 3] << 24);
}

/* Walk the captured stream sequentially (the compressor emits entries
 * back-to-back, every entry type starts with its little-endian magic; fields
 * are read byte-wise so no alignment assumption is made on the stream).
 * Counts the compressed args entries that belong to the probe fmtIds and
 * captures the first payload of each; also locates the format entries of a
 * few sentinel format strings and validates the custom string entry. */
STATIC UINT32 TestHplProbeWalk(VOID)
{
    const UINT8 *buf = g_hplProbeBuf;
    UINT32 total = g_hplProbeBytes;
    UINT32 i = 0;
    UINT32 r;
    UINT32 magic;
    UINT32 word;
    UINT32 tsBytes;
    UINT32 totalLen;
    UINT32 fmtId;
    UINT32 anBytes;
    UINT32 fileLen;
    UINT32 funcLen;
    UINT32 fmtLen;
    UINT32 lfFmtFound = 0;
    UINT32 upperXFmtFound = 0;
    UINT32 starStarFmtFound = 0;
    UINT32 customFound = 0;
    UINT32 customLenOk = 0;
    const CHAR *fmtStr;

    for (r = 0; r < g_probeRowCnt; r++) {
        g_probeSeen[r].entryCnt = 0;
        g_probeSeen[r].payloadLen = 0;
        g_probeSeen[r].payload = NULL;
    }

    while ((i + sizeof(UINT32)) <= total) {
        magic = TestHplProbeLe32(buf, i);
        if (magic == HPL_ARGS_ENTRY_MAGIC) {
            if ((i + sizeof(UINT32) * 3) > total) {
                break;
            }
            /* word = entryType:4 | additionalTimeStampBytes:4 | protocolVersion:8
             *        | totalEntryLength:16 (los_hpl_compression_pri.h:99-117). */
            word = TestHplProbeLe32(buf, i + 4);
            tsBytes = (word >> 4) & 0xF;
            totalLen = (word >> 16) & 0xFFFF;
            fmtId = TestHplProbeLe32(buf, i + 8);
            if ((tsBytes > sizeof(UINT64)) ||
                (totalLen < (sizeof(UINT32) * 3) + tsBytes) ||
                ((i + totalLen) > total)) {
                i++;
                continue;
            }
            for (r = 0; r < g_probeRowCnt; r++) {
                if ((g_probeRows[r].fmtId != LOS_HPL_UNASSIGNED_LOGID) &&
                    (HPL_GET_FMT_ID(g_probeRows[r].fmtId) == HPL_GET_FMT_ID(fmtId))) {
                    g_probeSeen[r].entryCnt++;
                    if (g_probeSeen[r].payload == NULL) {
                        g_probeSeen[r].payloadLen = totalLen - (UINT32)(sizeof(UINT32) * 3) - tsBytes;
                        g_probeSeen[r].payload = &buf[i + sizeof(UINT32) * 3 + tsBytes];
                    }
                    break;
                }
            }
            i += totalLen;
        } else if (magic == HPL_FMT_ENTRY_MAGIC) {
            if ((i + sizeof(UINT32) * 3) > total) {
                break;
            }
            /* word1 = entryType:4 | fmtAnalysisBytes:8 | linenum:20,
             * word2 = filenameLength:12 | funcStringLength:8 | formatStringLength:12. */
            anBytes = (TestHplProbeLe32(buf, i + 4) >> 4) & 0xFF;
            word = TestHplProbeLe32(buf, i + 8);
            fileLen = word & 0xFFF;
            funcLen = (word >> 12) & 0xFF;
            fmtLen = (word >> 20) & 0xFFF;
            if ((i + (sizeof(UINT32) * 3) + anBytes + fileLen + funcLen + fmtLen) > total) {
                i++;
                continue;
            }
            fmtStr = (const CHAR *)&buf[i + (sizeof(UINT32) * 3) + anBytes + fileLen + funcLen];
            if ((fmtLen == 4) && (memcmp(fmtStr, "%lf", 4) == 0)) { /* 4: "%lf\0" */
                lfFmtFound++;
            } else if ((fmtLen == 3) && (memcmp(fmtStr, "%X", 3) == 0)) { /* 3: "%X\0" */
                upperXFmtFound++;
            } else if ((fmtLen == 6) && (memcmp(fmtStr, "%*.*d", 6) == 0)) { /* 6: "%*.*d\0" */
                starStarFmtFound++;
            }
            i += (sizeof(UINT32) * 3) + anBytes + fileLen + funcLen + fmtLen;
        } else if (magic == HPL_ARGS_FRAGMENT_MAGIC) {
            /* Fragment header (magic + bitfield word + totalArgsEntrys), the
             * args entries follow right after it (los_hpl_compression.c:82-95). */
            i += sizeof(UINT32) * 3;
        } else if (magic == HPL_CHECKPOINT_MAGIC) {
            i += sizeof(UINT32) * 2; /* magic + version */
        } else if (magic == HPL_ABANDONED_LOG_ENTRY_MAGIC) {
            if ((i + sizeof(UINT32) * 2) > total) {
                break;
            }
            i += sizeof(UINT32) * 2 + TestHplProbeLe32(buf, i + 4) * sizeof(HplAbandonedLog);
        } else if (magic == HPL_USER_CUSTOM_STRING_MAGIC) {
            if ((i + HPL_PROBE_CUSTOM_HDR_SIZE) > total) {
                break;
            }
            customFound++;
            word = TestHplProbeLe32(buf, i + HPL_PROBE_CUSTOM_HDR_SIZE - 4); /* dataLength */
            if ((i + HPL_PROBE_CUSTOM_HDR_SIZE + word) <= total) {
                if ((word == sizeof(HPL_PROBE_CUSTOM_STR)) &&
                    (memcmp(&buf[i + HPL_PROBE_CUSTOM_HDR_SIZE], HPL_PROBE_CUSTOM_STR,
                            word) == 0)) {
                    customLenOk++;
                }
                i += HPL_PROBE_CUSTOM_HDR_SIZE + word;
            } else {
                i++;
            }
        } else {
            i++;
        }
    }

    /* The static info of the new call sites must be present in the stream,
     * including the three sentinels for the length modifier and the dynamic
     * width/precision combination. */
    ICUNIT_ASSERT_EQUAL(lfFmtFound, 1, lfFmtFound);
    ICUNIT_ASSERT_EQUAL(upperXFmtFound, 1, upperXFmtFound);
    ICUNIT_ASSERT_EQUAL(starStarFmtFound, 1, starStarFmtFound);
    ICUNIT_ASSERT_EQUAL(customFound, 1, customFound);
    ICUNIT_ASSERT_EQUAL(customLenOk, 1, customLenOk);
    return LOS_OK;
}

/* Worker: runs every probe exactly once through the runtime preprocessor
 * (OsHplLogger path, same as HP_LOG). DETACHED so its HPL staging handle is
 * freed by the preDelete/compress lifecycle and the ResLeak delta stays zero. */
static VOID TaskF15(VOID)
{
    UINT32 i;
    UINT32 wideType = (sizeof(UINTPTR) == sizeof(UINT32)) ? ARGS_TYPE_4_BYTES : ARGS_TYPE_8_BYTES;
    UINT32 archLongType = (sizeof(long) == sizeof(UINT32)) ? ARGS_TYPE_4_BYTES : ARGS_TYPE_8_BYTES;

    /* ---- Group A: conversion specifiers and length modifiers ---- */
    STATIC INT32 vD = -12345;
    HPL_PROBE_CALL("%d", vD);
    HPL_PROBE_ASSERT_NUM(1);
    HPL_PROBE_ASSERT_TYPE(0, ARGS_TYPE_4_BYTES);
    HPL_PROBE_STREAM(1, sizeof(vD), &vD, sizeof(vD));

    STATIC UINT32 vX = 0xDEADBEEFU;
    HPL_PROBE_CALL("%x", vX);
    HPL_PROBE_ASSERT_NUM(1);
    HPL_PROBE_ASSERT_TYPE(0, ARGS_TYPE_4_BYTES);
    HPL_PROBE_STREAM(1, sizeof(vX), &vX, sizeof(vX));

    STATIC UINT32 vXU = 0xABU;
    HPL_PROBE_CALL("%X", vXU);
    HPL_PROBE_ASSERT_NUM(1);
    HPL_PROBE_ASSERT_TYPE(0, ARGS_TYPE_4_BYTES);
    HPL_PROBE_STREAM(1, sizeof(vXU), &vXU, sizeof(vXU));

    STATIC UINT32 vU = 4000000000U;
    HPL_PROBE_CALL("%u", vU);
    HPL_PROBE_ASSERT_NUM(1);
    HPL_PROBE_ASSERT_TYPE(0, ARGS_TYPE_4_BYTES);
    HPL_PROBE_STREAM(1, sizeof(vU), &vU, sizeof(vU));

    STATIC INT32 vC = 'A';
    HPL_PROBE_CALL("%c", vC);
    HPL_PROBE_ASSERT_NUM(1);
    HPL_PROBE_ASSERT_TYPE(0, ARGS_TYPE_4_BYTES);
    HPL_PROBE_STREAM(1, sizeof(vC), &vC, sizeof(vC));

    STATIC CHAR vS[] = "HiHPL";
    HPL_PROBE_CALL("%s", vS);
    HPL_PROBE_ASSERT_NUM(1);
    HPL_PROBE_ASSERT_TYPE(0, ARGS_TYPE_STRING);
    HPL_PROBE_STREAM(1, sizeof(vS), vS, sizeof(vS));

    HPL_PROBE_CALL("%s", NULL);
    HPL_PROBE_ASSERT_NUM(1);
    HPL_PROBE_ASSERT_TYPE(0, ARGS_TYPE_STRING);
    HPL_PROBE_STREAM(1, 1, "", 1); /* NULL string is recorded as a single '\0'. */

    STATIC DOUBLE vF = 3.5;
    HPL_PROBE_CALL("%f", vF);
    HPL_PROBE_ASSERT_NUM(1);
    HPL_PROBE_ASSERT_TYPE(0, ARGS_TYPE_DOUBLE);
    HPL_PROBE_STREAM(1, sizeof(vF), &vF, sizeof(vF));

    STATIC DOUBLE vLF = 2.5;
    HPL_PROBE_CALL("%lf", vLF);
    HPL_PROBE_ASSERT_NUM(1);
    HPL_PROBE_ASSERT_TYPE(0, ARGS_TYPE_DOUBLE);
    HPL_PROBE_STREAM(1, sizeof(vLF), &vLF, sizeof(vLF));

    STATIC VOID *vP = (VOID *)(UINTPTR)0x2000ABCDU;
    HPL_PROBE_CALL("%p", vP);
    HPL_PROBE_ASSERT_NUM(1);
    HPL_PROBE_ASSERT_TYPE(0, wideType);
    HPL_PROBE_STREAM(1, sizeof(vP), &vP, sizeof(vP));

    STATIC long vLD = -1L;
    HPL_PROBE_CALL("%ld", vLD);
    HPL_PROBE_ASSERT_NUM(1);
    HPL_PROBE_ASSERT_TYPE(0, archLongType);
    HPL_PROBE_STREAM(1, sizeof(vLD), &vLD, sizeof(vLD));

    STATIC unsigned long vLU = 4000000000UL;
    HPL_PROBE_CALL("%lu", vLU);
    HPL_PROBE_ASSERT_NUM(1);
    HPL_PROBE_ASSERT_TYPE(0, archLongType);
    HPL_PROBE_STREAM(1, sizeof(vLU), &vLU, sizeof(vLU));

    STATIC unsigned long vLX = 0xBEEFUL;
    HPL_PROBE_CALL("%lx", vLX);
    HPL_PROBE_ASSERT_NUM(1);
    HPL_PROBE_ASSERT_TYPE(0, archLongType);
    HPL_PROBE_STREAM(1, sizeof(vLX), &vLX, sizeof(vLX));

    STATIC long long vLLD = 9007199254740993LL;
    HPL_PROBE_CALL("%lld", vLLD);
    HPL_PROBE_ASSERT_NUM(1);
    HPL_PROBE_ASSERT_TYPE(0, ARGS_TYPE_8_BYTES);
    HPL_PROBE_STREAM(1, sizeof(vLLD), &vLLD, sizeof(vLLD));

    STATIC unsigned long long vLLU = 18446744073709551615ULL;
    HPL_PROBE_CALL("%llu", vLLU);
    HPL_PROBE_ASSERT_NUM(1);
    HPL_PROBE_ASSERT_TYPE(0, ARGS_TYPE_8_BYTES);
    HPL_PROBE_STREAM(1, sizeof(vLLU), &vLLU, sizeof(vLLU));

    STATIC unsigned long long vLLX = 0x123456789ABCDEFULL;
    HPL_PROBE_CALL("%llx", vLLX);
    HPL_PROBE_ASSERT_NUM(1);
    HPL_PROBE_ASSERT_TYPE(0, ARGS_TYPE_8_BYTES);
    HPL_PROBE_STREAM(1, sizeof(vLLX), &vLLX, sizeof(vLLX));

    /* Composite: one call site with all families mixed, payload = off bytes
     * (4+5+8+8+4+4+4 = 37 for "cmix"). */
    STATIC INT32 cD = -7;
    STATIC CHAR cS[] = "cmix";
    STATIC DOUBLE cF = 1.5;
    STATIC long long cLLD = 0x1122334455667788LL;
    STATIC VOID *cP = (VOID *)(UINTPTR)0x3000U;
    STATIC INT32 cC = 'Q';
    STATIC UINT32 cU = 9;
    STATIC UINT8 cExp[40]; /* 40: upper bound, actual = off */
    UINT32 off = 0;
    (VOID)memcpy_s(&cExp[off], sizeof(cExp) - off, &cD, sizeof(cD));
    off += sizeof(cD);
    (VOID)memcpy_s(&cExp[off], sizeof(cExp) - off, cS, sizeof(cS));
    off += sizeof(cS);
    (VOID)memcpy_s(&cExp[off], sizeof(cExp) - off, &cF, sizeof(cF));
    off += sizeof(cF);
    (VOID)memcpy_s(&cExp[off], sizeof(cExp) - off, &cLLD, sizeof(cLLD));
    off += sizeof(cLLD);
    (VOID)memcpy_s(&cExp[off], sizeof(cExp) - off, &cP, sizeof(cP));
    off += sizeof(cP);
    (VOID)memcpy_s(&cExp[off], sizeof(cExp) - off, &cC, sizeof(cC));
    off += sizeof(cC);
    (VOID)memcpy_s(&cExp[off], sizeof(cExp) - off, &cU, sizeof(cU));
    off += sizeof(cU);
    HPL_PROBE_CALL("%d|%s|%f|%lld|%p|%c|%u", cD, cS, cF, cLLD, cP, cC, cU);
    HPL_PROBE_ASSERT_NUM(7);
    HPL_PROBE_ASSERT_TYPE(0, ARGS_TYPE_4_BYTES);
    HPL_PROBE_ASSERT_TYPE(1, ARGS_TYPE_STRING);
    HPL_PROBE_ASSERT_TYPE(2, ARGS_TYPE_DOUBLE);
    HPL_PROBE_ASSERT_TYPE(3, ARGS_TYPE_8_BYTES);
    HPL_PROBE_ASSERT_TYPE(4, wideType);
    HPL_PROBE_ASSERT_TYPE(5, ARGS_TYPE_4_BYTES);
    HPL_PROBE_ASSERT_TYPE(6, ARGS_TYPE_4_BYTES);
    HPL_PROBE_STREAM(1, off, cExp, off);

    /* ---- Group B: width / precision / flags / special formats ---- */
    STATIC UINT32 wStar = 6;
    STATIC INT32 vStar = 42;
    STATIC UINT8 expStarD[8]; /* 8: width(4) + value(4) */
    (VOID)memcpy_s(expStarD, sizeof(expStarD), &wStar, sizeof(wStar));
    (VOID)memcpy_s(&expStarD[4], sizeof(expStarD) - 4, &vStar, sizeof(vStar));
    HPL_PROBE_CALL("%*d", wStar, vStar);
    HPL_PROBE_ASSERT_NUM(2);
    HPL_PROBE_ASSERT_TYPE(0, ARGS_TYPE_4_BYTES); /* dynamic width first */
    HPL_PROBE_ASSERT_TYPE(1, ARGS_TYPE_4_BYTES);
    HPL_PROBE_STREAM(1, sizeof(expStarD), expStarD, sizeof(expStarD));

    STATIC CHAR sStar[] = "Hi";
    STATIC UINT8 expStarS[7]; /* 7: width(4) + "Hi\0"(3) */
    (VOID)memcpy_s(expStarS, sizeof(expStarS), &wStar, sizeof(wStar));
    (VOID)memcpy_s(&expStarS[4], sizeof(expStarS) - 4, sStar, sizeof(sStar));
    HPL_PROBE_CALL("%*s", wStar, sStar);
    HPL_PROBE_ASSERT_NUM(2);
    HPL_PROBE_ASSERT_TYPE(0, ARGS_TYPE_4_BYTES);
    HPL_PROBE_ASSERT_TYPE(1, ARGS_TYPE_STRING);
    HPL_PROBE_STREAM(1, sizeof(expStarS), expStarS, sizeof(expStarS));

    STATIC UINT32 pStar = 2;
    STATIC DOUBLE fStar = 3.5;
    STATIC UINT8 expDotF[12]; /* 12: precision(4) + double(8) */
    (VOID)memcpy_s(expDotF, sizeof(expDotF), &pStar, sizeof(pStar));
    (VOID)memcpy_s(&expDotF[4], sizeof(expDotF) - 4, &fStar, sizeof(fStar));
    HPL_PROBE_CALL("%.*f", pStar, fStar);
    HPL_PROBE_ASSERT_NUM(2);
    HPL_PROBE_ASSERT_TYPE(0, ARGS_TYPE_4_BYTES); /* dynamic precision first */
    HPL_PROBE_ASSERT_TYPE(1, ARGS_TYPE_DOUBLE);
    HPL_PROBE_STREAM(1, sizeof(expDotF), expDotF, sizeof(expDotF));

    STATIC UINT32 wSS = 5;
    STATIC UINT32 pSS = 3;
    STATIC INT32 vSS = 42;
    STATIC UINT8 expSSD[12]; /* 12: width(4) + precision(4) + value(4) */
    (VOID)memcpy_s(expSSD, sizeof(expSSD), &wSS, sizeof(wSS));
    (VOID)memcpy_s(&expSSD[4], sizeof(expSSD) - 4, &pSS, sizeof(pSS));
    (VOID)memcpy_s(&expSSD[8], sizeof(expSSD) - 8, &vSS, sizeof(vSS));
    HPL_PROBE_CALL("%*.*d", wSS, pSS, vSS);
    HPL_PROBE_ASSERT_NUM(3);
    HPL_PROBE_ASSERT_TYPE(0, ARGS_TYPE_4_BYTES); /* dynamic width first */
    HPL_PROBE_ASSERT_TYPE(1, ARGS_TYPE_4_BYTES); /* then dynamic precision */
    HPL_PROBE_ASSERT_TYPE(2, ARGS_TYPE_4_BYTES);
    HPL_PROBE_STREAM(1, sizeof(expSSD), expSSD, sizeof(expSSD));

    HPL_PROBE_CALL("%5.2f", vF); /* static width and precision: one argument. */
    HPL_PROBE_ASSERT_NUM(1);
    HPL_PROBE_ASSERT_TYPE(0, ARGS_TYPE_DOUBLE);
    HPL_PROBE_STREAM(1, sizeof(vF), &vF, sizeof(vF));

    HPL_PROBE_CALL("%-10s", vS); /* left align flag: one string argument. */
    HPL_PROBE_ASSERT_NUM(1);
    HPL_PROBE_ASSERT_TYPE(0, ARGS_TYPE_STRING);
    HPL_PROBE_STREAM(1, sizeof(vS), vS, sizeof(vS));

    HPL_PROBE_CALL("%#x", vXU); /* '#' flag: one argument. */
    HPL_PROBE_ASSERT_NUM(1);
    HPL_PROBE_ASSERT_TYPE(0, ARGS_TYPE_4_BYTES);
    HPL_PROBE_STREAM(1, sizeof(vXU), &vXU, sizeof(vXU));

    HPL_PROBE_CALL("%010d", vStar); /* zero padded width: one argument. */
    HPL_PROBE_ASSERT_NUM(1);
    HPL_PROBE_ASSERT_TYPE(0, ARGS_TYPE_4_BYTES);
    HPL_PROBE_STREAM(1, sizeof(vStar), &vStar, sizeof(vStar));

    HPL_PROBE_CALL("% d", vStar); /* space flag treated as "%d": one argument. */
    HPL_PROBE_ASSERT_NUM(1);
    HPL_PROBE_ASSERT_TYPE(0, ARGS_TYPE_4_BYTES);
    HPL_PROBE_STREAM(1, sizeof(vStar), &vStar, sizeof(vStar));

    HPL_PROBE_CALL("%%"); /* literal percent: no argument. */
    HPL_PROBE_ASSERT_NUM(0);
    HPL_PROBE_STREAM(1, 0, NULL, 0);

    STATIC INT32 vPct = 7;
    HPL_PROBE_CALL("%d%%", vPct); /* trailing literal percent: one argument. */
    HPL_PROBE_ASSERT_NUM(1);
    HPL_PROBE_ASSERT_TYPE(0, ARGS_TYPE_4_BYTES);
    HPL_PROBE_STREAM(1, sizeof(vPct), &vPct, sizeof(vPct));

    HPL_PROBE_CALL(""); /* empty format: no argument, still one entry. */
    HPL_PROBE_ASSERT_NUM(0);
    HPL_PROBE_STREAM(1, 0, NULL, 0);

    /* Known analyzer limitation (characterization, registered in the design
     * docs): conversions the analyzer does not support are skipped and their
     * arguments are silently not recorded. The arguments below are chosen so
     * the case stays safe even if the analyzer later learns these specs. */
    HPL_PROBE_CALL("%o", vStar);
    HPL_PROBE_ASSERT_NUM(0);
    HPL_PROBE_STREAM(1, 0, NULL, 0);

    HPL_PROBE_CALL("%i", vStar);
    HPL_PROBE_ASSERT_NUM(0);
    HPL_PROBE_STREAM(1, 0, NULL, 0);

    HPL_PROBE_CALL("%hd", vStar);
    HPL_PROBE_ASSERT_NUM(0);
    HPL_PROBE_STREAM(1, 0, NULL, 0);

    HPL_PROBE_CALL("%.f", vF); /* dot without digits also drops the argument. */
    HPL_PROBE_ASSERT_NUM(0);
    HPL_PROBE_STREAM(1, 0, NULL, 0);

    STATIC CHAR wStr[] = "notwide"; /* safe under either the current or a future %ls handling. */
    HPL_PROBE_CALL("%ls", wStr);
    HPL_PROBE_ASSERT_NUM(0);
    HPL_PROBE_STREAM(1, 0, NULL, 0);

    /* ---- Group C: boundaries ---- */
    STATIC UINT32 v28[28];
    for (i = 0; i < 28; i++) {
        v28[i] = i + 1;
    }
    HPL_PROBE_CALL("%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u",
                   v28[0], v28[1], v28[2], v28[3], v28[4], v28[5], v28[6], v28[7],
                   v28[8], v28[9], v28[10], v28[11], v28[12], v28[13], v28[14],
                   v28[15], v28[16], v28[17], v28[18], v28[19], v28[20], v28[21],
                   v28[22], v28[23], v28[24], v28[25], v28[26], v28[27]);
    HPL_PROBE_ASSERT_NUM(28);
    for (i = 0; i < 28; i++) {
        HPL_PROBE_ASSERT_TYPE(i, ARGS_TYPE_4_BYTES);
    }
    HPL_PROBE_STREAM(1, sizeof(v28), v28, sizeof(v28));

    HPL_PROBE_CALL("%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u",
                   v28[0], v28[1], v28[2], v28[3], v28[4], v28[5], v28[6], v28[7],
                   v28[8], v28[9], v28[10], v28[11], v28[12], v28[13], v28[14],
                   v28[15], v28[16], v28[17], v28[18], v28[19], v28[20], v28[21],
                   v28[22], v28[23], v28[24], v28[25], v28[26], v28[27], vU);
    /* 29 arguments exceed HPL_MAX_ARGS_NUM(28): the analysis is rejected, the
     * fmtId stays unassigned and no entry can appear in the stream. */
    HPL_PROBE_ASSERT_REJECT();
    HPL_PROBE_STREAM(0, 0, NULL, 0);

    HPL_PROBE_CALL("%s", g_str700); /* 700 chars + '\0' = 701 bytes payload. */
    HPL_PROBE_ASSERT_NUM(1);
    HPL_PROBE_ASSERT_TYPE(0, ARGS_TYPE_STRING);
    HPL_PROBE_STREAM(1, sizeof(g_str700), g_str700, sizeof(g_str700));

    HPL_PROBE_CALL("%s", g_str727); /* 727 chars + '\0' = 728 bytes, exact fit. */
    HPL_PROBE_ASSERT_NUM(1);
    HPL_PROBE_ASSERT_TYPE(0, ARGS_TYPE_STRING);
    HPL_PROBE_STREAM(1, sizeof(g_str727), g_str727, sizeof(g_str727));

    HPL_PROBE_CALL("%s", g_str750);
    /* 750 chars + '\0' = 751 bytes overflows the 728 byte argument area: the
     * format is registered but the record is dropped, no args entry appears. */
    HPL_PROBE_ASSERT_NUM(1);
    HPL_PROBE_ASSERT_TYPE(0, ARGS_TYPE_STRING);
    HPL_PROBE_ASSERT_ASSIGNED();
    HPL_PROBE_STREAM(0, 0, NULL, 0);

    /* Custom string entry: recorded directly as a custom magic entry whose
     * payload layout is verified by the walker. */
    (VOID)LOS_HplRecordCustomString(HPL_PROBE_CUSTOM_STR, sizeof(HPL_PROBE_CUSTOM_STR));

    g_testCount = 1;
}

static UINT32 TestCase(VOID)
{
    UINT32 i;
    UINT32 wait = 0;
    UINT32 fmtBefore;
    UINT32 ret;
    TSK_INIT_PARAM_S taskParam = {0};

    /* Deterministic fixtures. */
    (VOID)memset_s(g_str700, sizeof(g_str700), 'B', HPL_PROBE_LONG_STR_700);
    g_str700[HPL_PROBE_LONG_STR_700] = '\0';
    (VOID)memset_s(g_str727, sizeof(g_str727), 'C', HPL_PROBE_LONG_STR_727);
    g_str727[HPL_PROBE_LONG_STR_727] = '\0';
    (VOID)memset_s(g_str750, sizeof(g_str750), 'D', HPL_PROBE_LONG_STR_750);
    g_str750[HPL_PROBE_LONG_STR_750] = '\0';

    g_probeRowCnt = 0;
    g_probeErrRow = HPL_PROBE_ERR_NONE;
    g_hplProbeBytes = 0;
    ret = LOS_HplSetLogRecordHook(TestHplProbeHook);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_HplSetLogLevel(HPL_TEST_LOG_LEVEL);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    fmtBefore = g_hplFmtEntryCnt;

    taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)TaskF15;
    taskParam.uwStackSize = TASK_STACK_SIZE_TEST;
    taskParam.pcName = "HplFmt015";
    taskParam.usTaskPrio = TASK_PRIO_TEST - 1;
    taskParam.uwResved = LOS_TASK_STATUS_DETACHED;
    g_testCount = 0; /* Must be cleared before create: the higher-prio worker may finish during it. */
    ret = LOS_TaskCreate(&g_testTaskID01, &taskParam);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    while ((g_testCount != 1) && (wait < HPL_TEST_WAIT_LIMIT)) {
        LOS_TaskDelay(1);
        wait++;
    }
    ICUNIT_ASSERT_EQUAL(g_testCount, 1, g_testCount);

    /* Drain: the compress task flushes the staging buffers on its event or on
     * its 1000ms sleep timeout and processes the pending preDelete frees,
     * both covered by HPL_TEST_DRAIN_TICKS(120). */
    LOS_TaskDelay(HPL_TEST_DRAIN_TICKS);

    ICUNIT_ASSERT_EQUAL(g_probeRowCnt, 39, g_probeRowCnt);
    /* All runtime-analysis expectations held in the worker (row/kind encoded
     * in the retcode on failure). */
    ICUNIT_ASSERT_EQUAL(g_probeErrRow, HPL_PROBE_ERR_NONE,
                        (g_probeErrRow << 4) | (g_probeErrKind & 0xF));

    ret = TestHplProbeWalk();
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* Per-format stream outcome: exactly the expected number of entries with
     * exactly the expected payload size and content. */
    for (i = 0; i < g_probeRowCnt; i++) {
        ICUNIT_ASSERT_EQUAL(g_probeSeen[i].entryCnt, g_probeRows[i].wantEntries,
                            (i << 16) | g_probeSeen[i].entryCnt);
        if (g_probeRows[i].wantEntries == 0) {
            continue;
        }
        ICUNIT_ASSERT_EQUAL(g_probeSeen[i].payloadLen, g_probeRows[i].wantPayload,
                            (i << 16) | g_probeSeen[i].payloadLen);
        if (g_probeRows[i].content != NULL) {
            ICUNIT_ASSERT_EQUAL(memcmp(g_probeSeen[i].payload, g_probeRows[i].content,
                                       g_probeRows[i].contentLen), 0, i);
        }
    }

    /* Every accepted call site registered exactly one format entry (the 29
     * argument probe is rejected before registration). */
    ICUNIT_ASSERT_EQUAL((g_hplFmtEntryCnt - fmtBefore) >= (g_probeRowCnt - 1), 1,
                        g_hplFmtEntryCnt - fmtBefore);

    /* Restore the shared statistics hook and level for the following cases. */
    TestHplPrepare();
    return LOS_OK;
}

VOID ItLosHpl015(VOID)
{
    TEST_ADD_CASE("ItLosHpl015", TestCase, TEST_LOS, TEST_HPL, TEST_LEVEL0, TEST_FUNCTION);
}
