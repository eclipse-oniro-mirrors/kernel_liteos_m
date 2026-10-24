/*
 * Copyright (c) 2026-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted under the terms of the license (described in detail in the
 * LICENSE file provided with this code).
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "osTest.h"
#include "It_los_hpl.h"
#include <fcntl.h>
#include <unistd.h>

/* RAM buffer collecting the compressed byte stream seen by the record hook
 * during this case. It is dumped to the serial line as HEX lines prefixed
 * with "HPLDUMP:" so the host-side script (tools/hpl_e2e.py) can rebuild the
 * binary stream, decompress it with the mkDecompressor host tool and compare
 * the restored text against the anchors below. The stream is additionally
 * persisted to /littlefs (write + read-back verify), mirroring the real
 * product flow of storing the log file on a flash filesystem. */
#define HPL_E2E_BUF_SIZE        8192
#define HPL_E2E_LOG_COUNT       10
#define HPL_E2E_DUMP_WIDTH      32
#define HPL_E2E_DUMP_FILE       "/littlefs/hpl_dump.bin"

STATIC UINT8 g_e2eBuf[HPL_E2E_BUF_SIZE];
STATIC volatile UINT32 g_e2eBytes;

/* ARGS_RECORD_FUNC, runs in the compression task context. */
STATIC UINT32 TestHplCollectHook(const UINT8 *buf, UINT32 size)
{
    if ((buf == NULL) || (size == 0)) {
        return LOS_OK;
    }
    if ((g_e2eBytes + size) > HPL_E2E_BUF_SIZE) {
        return LOS_OK; /* Guard only: the case stays far below the limit. */
    }
    if (memcpy_s(&g_e2eBuf[g_e2eBytes], HPL_E2E_BUF_SIZE - g_e2eBytes, buf, size) != EOK) {
        return LOS_NOK;
    }
    g_e2eBytes += size;
    return LOS_OK;
}

/* Emit the anchors from a dedicated worker task: logging allocates the task
 * HPL staging buffer, and the worker is reclaimed through the preDelete
 * path on exit, keeping the case leak free for ResLeak. */
STATIC VOID TaskF31(VOID)
{
    /* Anchors: every format string is unique so each call emits one format
     * entry plus one args entry. Values are chosen to be reproducible in the
     * decompressed text on the host (see tools/hpl_e2e.py). */
    HP_LOG(HPL_DEBUG, "e2e01 d=%d\n", 42);
    HP_LOG(HPL_DEBUG, "e2e02 s=%s\n", "endtoend");
    HP_LOG(HPL_DEBUG, "e2e03 x=0x%x\n", 0xABCD);
    HP_LOG(HPL_DEBUG, "e2e04 u=%u\n", 123456789);
    HP_LOG(HPL_DEBUG, "e2e05 c=%c\n", 'Z');
    HP_LOG(HPL_DEBUG, "e2e06 ld=%ld\n", -100000L);
    HP_LOG(HPL_DEBUG, "e2e07 lld=%lld\n", 9007199254740993LL);
    HP_LOG(HPL_DEBUG, "e2e08 f=%f\n", 3.14);
    HP_LOG(HPL_DEBUG, "e2e09 pct=%%d done\n");
    HP_LOG(HPL_DEBUG, "e2e10 p=%p\n", (VOID *)0x1234);
    g_testCount = 1;
}

/* Persist the stream through VFS/littlefs using plain POSIX I/O: the musl
 * stdio write path on this platform has a registered defect
 * (__stdio_write's iov handling on a zero-length VfsWrite, see the header
 * note in testsuites/unittest/posix/src/stdio/stdio_func_test.c), while a
 * single direct write() is the verified-good path. The deep
 * open->VFS->littlefs call chain overflows the 0x1000 test-runner stack
 * (verified by an OS exception dump), so the work runs in a dedicated task
 * with headroom and reports the outcome through the g_e2eFile* flags. */
STATIC volatile UINT32 g_e2eFileDone;
STATIC volatile UINT32 g_e2eFileRc;
#define HPL_E2E_FILE_STACK_SIZE 0x2000

STATIC VOID TaskF31File(VOID)
{
    INT32 fd;
    INT32 io;
    static UINT8 readBackBuf[HPL_E2E_BUF_SIZE];

    g_e2eFileRc = LOS_NOK;
    fd = open(HPL_E2E_DUMP_FILE, O_CREAT | O_WRONLY | O_TRUNC, 0777);
    if (fd < 0) {
        goto OUT;
    }
    io = write(fd, g_e2eBuf, g_e2eBytes);
    (VOID)close(fd);
    if (io != (INT32)g_e2eBytes) {
        goto OUT;
    }

    fd = open(HPL_E2E_DUMP_FILE, O_RDONLY);
    if (fd < 0) {
        goto OUT;
    }
    io = read(fd, readBackBuf, HPL_E2E_BUF_SIZE);
    (VOID)close(fd);
    if ((io != (INT32)g_e2eBytes) || (memcmp(readBackBuf, g_e2eBuf, g_e2eBytes) != 0)) {
        goto OUT;
    }
    printf("HPLDUMP_FILE:%s:%u\n", HPL_E2E_DUMP_FILE, g_e2eBytes);
    g_e2eFileRc = LOS_OK;
OUT:
    g_e2eFileDone = 1;
}

STATIC UINT32 TestCase(VOID)
{
    UINT32 i;
    UINT32 j;
    UINT32 end;
    UINT32 magic;
    UINT32 argsCnt = 0;
    UINT32 fmtCnt = 0;
    UINT32 ret;
    UINT32 wait = 0;
    TSK_INIT_PARAM_S taskParam = {0};

    g_e2eBytes = 0;
    g_testCount = 0;
    (VOID)LOS_HplSetLogRecordHook(TestHplCollectHook);

    taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)TaskF31;
    taskParam.uwStackSize = TASK_STACK_SIZE_TEST;
    taskParam.pcName = "HplE2e031";
    taskParam.usTaskPrio = TASK_PRIO_TEST - 1;
    taskParam.uwResved = LOS_TASK_STATUS_DETACHED;
    ret = LOS_TaskCreate(&g_testTaskID01, &taskParam);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    while ((g_testCount != 1) && (wait < HPL_TEST_WAIT_LIMIT)) {
        LOS_TaskDelay(1);
        wait++;
    }
    ICUNIT_ASSERT_EQUAL(g_testCount, 1, g_testCount);
    LOS_TaskDelay(HPL_TEST_DRAIN_TICKS);

    /* On-board self check: the stream must hold one format entry and one
     * args entry per anchor log. */
    for (i = 0; (i + sizeof(UINT32)) <= g_e2eBytes; i++) {
        magic = (UINT32)g_e2eBuf[i] | ((UINT32)g_e2eBuf[i + 1] << 8) |
                ((UINT32)g_e2eBuf[i + 2] << 16) | ((UINT32)g_e2eBuf[i + 3] << 24);
        if (magic == HPL_ARGS_ENTRY_MAGIC) {
            argsCnt++;
        } else if (magic == HPL_FMT_ENTRY_MAGIC) {
            fmtCnt++;
        }
    }
    ICUNIT_ASSERT_EQUAL((g_e2eBytes > 0), 1, g_e2eBytes);
    ICUNIT_ASSERT_EQUAL((argsCnt >= HPL_E2E_LOG_COUNT), 1, argsCnt);
#ifdef LOSCFG_KERNEL_HPL_SCRIPT_PREPROCESSOR
    /* Script mode: static info lives in the build-time metadata file, the
     * stream itself carries no format entry. */
    ICUNIT_ASSERT_EQUAL(fmtCnt, 0, fmtCnt);
#else
    ICUNIT_ASSERT_EQUAL((fmtCnt >= HPL_E2E_LOG_COUNT), 1, fmtCnt);
#endif

    /* Serial HEX dump for the host-side end-to-end comparison. */
    printf("HPLDUMP_BEGIN:%u\n", g_e2eBytes);
    for (i = 0; i < g_e2eBytes; i += HPL_E2E_DUMP_WIDTH) {
        end = (i + HPL_E2E_DUMP_WIDTH) < g_e2eBytes ? (i + HPL_E2E_DUMP_WIDTH) : g_e2eBytes;
        printf("HPLDUMP:");
        for (j = i; j < end; j++) {
            printf("%02x", g_e2eBuf[j]);
        }
        printf("\n");
    }
    printf("HPLDUMP_END\n");

    /* Restore the statistics hook used by the other cases. */
    (VOID)LOS_HplSetLogRecordHook(TestHplRecordHook);

    /* Persist the stream through VFS/littlefs in a dedicated task (see
     * TaskF31File for why the stack has to be enlarged). Kept after the hook
     * restore so the filesystem work runs with HPL fully idle. */
    g_e2eFileDone = 0;
    taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)TaskF31File;
    taskParam.uwStackSize = HPL_E2E_FILE_STACK_SIZE;
    taskParam.pcName = "HplE2eFile";
    taskParam.usTaskPrio = TASK_PRIO_TEST - 1;
    taskParam.uwResved = LOS_TASK_STATUS_DETACHED;
    ret = LOS_TaskCreate(&g_testTaskID01, &taskParam);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    wait = 0;
    while ((g_e2eFileDone != 1) && (wait < HPL_TEST_WAIT_LIMIT)) {
        LOS_TaskDelay(1);
        wait++;
    }
    ICUNIT_ASSERT_EQUAL(g_e2eFileDone, 1, g_e2eFileDone);
    ICUNIT_ASSERT_EQUAL(g_e2eFileRc, LOS_OK, g_e2eFileRc);
    return LOS_OK;
}

VOID ItLosHpl031(VOID) // IT_Layer_ModuleORFeature_No
{
    TEST_ADD_CASE("ItLosHpl031", TestCase, TEST_LOS, TEST_HPL, TEST_LEVEL0, TEST_FUNCTION);
}
