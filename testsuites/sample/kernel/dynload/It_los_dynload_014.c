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
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF ANY SUCH DAMAGE.
 */
#include "los_ld_elflib.h"
#include "los_memory.h"
#include "It_los_dynload.h"
#include "securec.h"
#include "string.h"

/* 用例简要描述: 损坏 ELF 与参数不一致负面注入 */
typedef struct {
    unsigned char eIdent[16];
    unsigned short eType;
    unsigned short eMachine;
    unsigned int eVersion;
    unsigned int eEntry;
    unsigned int ePhoff;
    unsigned int eShoff;
    unsigned int eFlags;
    unsigned short eEhsize;
    unsigned short ePhentsize;
    unsigned short ePhnum;
    unsigned short eShentsize;
    unsigned short eShnum;
    unsigned short eShstrndx;
} TestElf32Ehdr;

typedef struct {
    unsigned int shName;
    unsigned int shType;
    unsigned int shFlags;
    unsigned int shAddr;
    unsigned int shOffset;
    unsigned int shSize;
    unsigned int shLink;
    unsigned int shInfo;
    unsigned int shAddralign;
    unsigned int shEntsize;
} TestElf32Shdr;

typedef struct {
    unsigned int stName;
    unsigned int stValue;
    unsigned int stSize;
    unsigned char stInfo;
    unsigned char stOther;
    unsigned short stShndx;
} TestElf32Sym;

typedef struct {
    unsigned int rOffset;
    unsigned int rInfo;
    unsigned int rAddend;
} TestElf32Rela;

typedef struct {
    unsigned int pType;
    unsigned int pOffset;
    unsigned int pVaddr;
    unsigned int pPaddr;
    unsigned int pFilesz;
    unsigned int pMemsz;
    unsigned int pFlags;
    unsigned int pAlign;
} TestElf32Phdr;

typedef struct {
    unsigned int dTag;
    unsigned int dVal;
} TestElf32Dyn;

extern const UINT8 _binary_objbasic_o_start[];
extern const UINT8 _binary_objbasic_o_end[];
extern const UINT8 _binary_dep_a_so_start[];
extern const UINT8 _binary_dep_a_so_end[];
extern const UINT8 _binary_basic_so_start[];
extern const UINT8 _binary_basic_so_end[];

#define DYNLOAD_MUTBUF_SIZE 3072

/* 变异缓冲从系统堆临时分配（TestCase 入口申请、全路径释放），避免 .bss 常驻
 * 压缩全周程堆预算（hi3322 系统堆仅 ~197KB，Shell/FS 域堆敏感用例会被压垮） */
STATIC UINT8 *g_mutBuf = NULL;

STATIC UINT8 *MutCopy(const UINT8 *start, const UINT8 *end)
{
    UINT32 len = (UINT32)(end - start);
    if (len > DYNLOAD_MUTBUF_SIZE) {
        return NULL;
    }
    if (memcpy_s(g_mutBuf, DYNLOAD_MUTBUF_SIZE, start, len) != EOK) {
        return NULL;
    }
    return g_mutBuf;
}

STATIC TestElf32Shdr *MutFindScn(UINT8 *img, const CHAR *name)
{
    TestElf32Ehdr *ehdr = (TestElf32Ehdr *)img;
    TestElf32Shdr *shdrs = (TestElf32Shdr *)(img + ehdr->eShoff);
    const CHAR *shstrtab = (const CHAR *)(img + shdrs[ehdr->eShstrndx].shOffset);
    UINT32 idx;

    for (idx = 0; idx < ehdr->eShnum; idx++) {
        if (strcmp(shstrtab + shdrs[idx].shName, name) == 0) {
            return &shdrs[idx];
        }
    }
    return NULL;
}

/* 定位 PT_DYNAMIC 内指定 tag 的条目（basic.so 为 identity 映射，vaddr 即文件偏移） */
STATIC TestElf32Dyn *MutFindDynEntry(UINT8 *img, UINT32 tag)
{
    TestElf32Ehdr *ehdr = (TestElf32Ehdr *)img;
    TestElf32Phdr *phdrs = (TestElf32Phdr *)(img + ehdr->ePhoff);
    TestElf32Dyn *dyn = NULL;
    UINT32 idx;

    for (idx = 0; idx < ehdr->ePhnum; idx++) {
        if (phdrs[idx].pType == 2) { /* PT_DYNAMIC */
            dyn = (TestElf32Dyn *)(img + phdrs[idx].pOffset);
            break;
        }
    }
    while ((dyn != NULL) && (dyn->dTag != 0)) {
        if (dyn->dTag == tag) {
            return dyn;
        }
        dyn++;
    }
    return NULL;
}

STATIC UINT32 TestCase(VOID)
{
    UINT8 *img = NULL;
    TestElf32Shdr *scn = NULL;
    TestElf32Rela *rela = NULL;
    TestElf32Sym *sym = NULL;
    TestElf32Dyn *dyn = NULL;
    CHAR *str = NULL;
    VOID *badHandle = NULL;
    UINT32 imgLen;

    g_mutBuf = LOS_MemAlloc((VOID *)m_aucSysMem0, DYNLOAD_MUTBUF_SIZE);
    ICUNIT_GOTO_NOT_EQUAL(g_mutBuf, NULL, 0, EXIT);

    /* 未知重定位类型被拒绝 */
    img = MutCopy(_binary_objbasic_o_start, _binary_objbasic_o_end);
    ICUNIT_GOTO_NOT_EQUAL(img, NULL, img, EXIT);
    scn = MutFindScn(img, ".rela.text");
    ICUNIT_GOTO_NOT_EQUAL(scn, NULL, scn, EXIT);
    rela = (TestElf32Rela *)(img + scn->shOffset);
    rela->rInfo = (rela->rInfo & ~0xffU) | 0xF0U;
    imgLen = (UINT32)(_binary_objbasic_o_end - _binary_objbasic_o_start);
    badHandle = LOS_MemLoad("mut1", strlen("mut1"), (const CHAR *)img, imgLen);
    ICUNIT_GOTO_EQUAL(badHandle, NULL, badHandle, EXIT);

    /* 符号表 0 号表项被占用被拒绝（stInfo 须为 NOTYPE） */
    img = MutCopy(_binary_objbasic_o_start, _binary_objbasic_o_end);
    ICUNIT_GOTO_NOT_EQUAL(img, NULL, img, EXIT);
    scn = MutFindScn(img, ".symtab");
    ICUNIT_GOTO_NOT_EQUAL(scn, NULL, scn, EXIT);
    sym = (TestElf32Sym *)(img + scn->shOffset);
    sym->stInfo = 0x12; /* GLOBAL FUNC: 非 NOTYPE */
    badHandle = LOS_MemLoad("mut4", strlen("mut4"), (const CHAR *)img, imgLen);
    ICUNIT_GOTO_EQUAL(badHandle, NULL, badHandle, EXIT);

    /* DT_NEEDED 依赖名不可解析被拒绝 */
    img = MutCopy(_binary_dep_a_so_start, _binary_dep_a_so_end);
    ICUNIT_GOTO_NOT_EQUAL(img, NULL, img, EXIT);
    imgLen = (UINT32)(_binary_dep_a_so_end - _binary_dep_a_so_start);
    scn = MutFindScn(img, ".dynstr");
    ICUNIT_GOTO_NOT_EQUAL(scn, NULL, scn, EXIT);
    str = (CHAR *)(img + scn->shOffset);
    if (strstr(str, "dep_b.so") != NULL) {
        *strstr(str, "dep_b.so") = 'X';
    }
    badHandle = LOS_MemLoad("mut5", strlen("mut5"), (const CHAR *)img, imgLen);
    ICUNIT_GOTO_EQUAL(badHandle, NULL, badHandle, EXIT);

    /* 文件名长度与内容不一致被拒绝 */
    img = MutCopy(_binary_objbasic_o_start, _binary_objbasic_o_end);
    ICUNIT_GOTO_NOT_EQUAL(img, NULL, img, EXIT);
    imgLen = (UINT32)(_binary_objbasic_o_end - _binary_objbasic_o_start);
    badHandle = LOS_MemLoad("mismatch", 3, (const CHAR *)img, imgLen);
    ICUNIT_GOTO_EQUAL(badHandle, NULL, badHandle, EXIT);

    /* ---- 以下针对 ET_DYN（basic.so）：动态段/重定位表变异 ---- */

    /* DT_SYMENT 值非法被拒绝 */
    img = MutCopy(_binary_basic_so_start, _binary_basic_so_end);
    ICUNIT_GOTO_NOT_EQUAL(img, NULL, img, EXIT);
    imgLen = (UINT32)(_binary_basic_so_end - _binary_basic_so_start);
    dyn = MutFindDynEntry(img, 0x0b /* DT_SYMENT */);
    ICUNIT_GOTO_NOT_EQUAL(dyn, NULL, dyn, EXIT);
    dyn->dVal = 5;
    badHandle = LOS_MemLoad("mut6", strlen("mut6"), (const CHAR *)img, imgLen);
    ICUNIT_GOTO_EQUAL(badHandle, NULL, badHandle, EXIT);

    /* DT_RELAENT 值非法被拒绝 */
    img = MutCopy(_binary_basic_so_start, _binary_basic_so_end);
    ICUNIT_GOTO_NOT_EQUAL(img, NULL, img, EXIT);
    dyn = MutFindDynEntry(img, 0x09 /* DT_RELAENT */);
    ICUNIT_GOTO_NOT_EQUAL(dyn, NULL, dyn, EXIT);
    dyn->dVal = 5;
    badHandle = LOS_MemLoad("mut8", strlen("mut8"), (const CHAR *)img, imgLen);
    ICUNIT_GOTO_EQUAL(badHandle, NULL, badHandle, EXIT);

    /* ET_DYN 重定位条目类型未知被拒绝 */
    img = MutCopy(_binary_basic_so_start, _binary_basic_so_end);
    ICUNIT_GOTO_NOT_EQUAL(img, NULL, img, EXIT);
    dyn = MutFindDynEntry(img, 0x07 /* DT_RELA */);
    ICUNIT_GOTO_NOT_EQUAL(dyn, NULL, dyn, EXIT);
    rela = (TestElf32Rela *)(img + dyn->dVal);
    rela->rInfo = (rela->rInfo & ~0xffU) | 0xF0U;
    badHandle = LOS_MemLoad("mut9", strlen("mut9"), (const CHAR *)img, imgLen);
    ICUNIT_GOTO_EQUAL(badHandle, NULL, badHandle, EXIT);

    /* ---- 以下针对 ET_REL（objbasic.o）：头/节表变异 ---- */

    /* ELF 类型非 ET_REL 被拒绝 */
    img = MutCopy(_binary_objbasic_o_start, _binary_objbasic_o_end);
    ICUNIT_GOTO_NOT_EQUAL(img, NULL, img, EXIT);
    imgLen = (UINT32)(_binary_objbasic_o_end - _binary_objbasic_o_start);
    ((TestElf32Ehdr *)img)->eType = 2; /* ET_EXEC */
    badHandle = LOS_MemLoad("mut10", strlen("mut10"), (const CHAR *)img, imgLen);
    ICUNIT_GOTO_EQUAL(badHandle, NULL, badHandle, EXIT);

    /* 重定位节 shLink 越界被拒绝 */
    img = MutCopy(_binary_objbasic_o_start, _binary_objbasic_o_end);
    ICUNIT_GOTO_NOT_EQUAL(img, NULL, img, EXIT);
    scn = MutFindScn(img, ".rela.text");
    ICUNIT_GOTO_NOT_EQUAL(scn, NULL, scn, EXIT);
    scn->shLink = 99;
    badHandle = LOS_MemLoad("mut11", strlen("mut11"), (const CHAR *)img, imgLen);
    ICUNIT_GOTO_EQUAL(badHandle, NULL, badHandle, EXIT);

    /* 重定位类型在处理表内但无处理器（未注册槽位）被拒绝 */
    img = MutCopy(_binary_objbasic_o_start, _binary_objbasic_o_end);
    ICUNIT_GOTO_NOT_EQUAL(img, NULL, img, EXIT);
    scn = MutFindScn(img, ".rela.text");
    ICUNIT_GOTO_NOT_EQUAL(scn, NULL, scn, EXIT);
    rela = (TestElf32Rela *)(img + scn->shOffset);
    rela->rInfo = (rela->rInfo & ~0xffU) | 0x02U;
    badHandle = LOS_MemLoad("mut13", strlen("mut13"), (const CHAR *)img, imgLen);
    ICUNIT_GOTO_EQUAL(badHandle, NULL, badHandle, EXIT);

EXIT:
    if (g_mutBuf != NULL) {
        (VOID)LOS_MemFree((VOID *)m_aucSysMem0, g_mutBuf);
        g_mutBuf = NULL;
    }
    return LOS_OK;
}

VOID ItLosDynload014(VOID)
{
    TEST_ADD_CASE("ItLosDynload014", TestCase, TEST_LOS, TEST_DYNLOAD, TEST_LEVEL1, TEST_FUNCTION);
}
