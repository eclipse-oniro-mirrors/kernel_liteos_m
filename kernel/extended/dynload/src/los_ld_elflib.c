/*
 * Copyright (c) 2026-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 *    of conditions and the following disclaimer in the documentation and/or other materials
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
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "los_ld_elflib_pri.h"
#include "los_ld_macro_pri.h"
#include "los_ld_commlib_pri.h"
#include "los_ld_reloc_pri.h"
#include "los_ld_modulelib_pri.h"
#include "los_ld_dllib_pri.h"
#include "los_ld_symlib_pri.h"

#ifdef LOSCFG_DYNLOAD_REL_FROM_FS
#include "fcntl.h"
#include "unistd.h"
#include "sys/stat.h"
#include "limits.h"
#endif

#include "los_hwi.h"
#include "los_memory.h"

#ifndef SECTION_ALIGN_SIZE
#define SECTION_ALIGN_SIZE sizeof(UINTPTR)
#endif


#ifdef LOSCFG_KERNEL_DYNLOAD_REL

#ifdef LOSCFG_DYNLOAD_LITE_SYMTAB
#define LD_LITE_SYM_TAB_NAME        ".liteos.sym.table"
STATIC INLINE const CHAR *LD_ObjSectionNameRead(const LD_ELF_SHDR *shstrtab, const LD_ELF_SHDR *section)
{
    return (CHAR *)shstrtab->shOffset + section->shName;
}

typedef struct LiteSymTabHeader {
    UINT32 symTabNum;
    UINT32 reserved[3];             /* reserved for future */
    CHAR   content[];
} LiteSymTabHeader;

UINT32 LD_ObjGetLiteSymTab(DlInfo *dlInfo, const LD_ELF_EHDR *ehdr, const LD_ELF_SHDR *scnHdrTbl)
{
    LiteSymTabHeader *symTabHdr = NULL;
    const LD_ELF_SHDR *section = NULL;
    const CHAR *sectionName = NULL;
    const LD_ELF_SHDR *shstrtab = &scnHdrTbl[ehdr->elfShStrIndex];
    for (INT32 idx = 0; idx < ehdr->elfShNum; idx++) {
        section = &scnHdrTbl[idx];
        sectionName = LD_ObjSectionNameRead(shstrtab, section);
        if (strcmp(sectionName, LD_LITE_SYM_TAB_NAME) == 0) {
            if (section->shSize < sizeof(LiteSymTabHeader)) {
                return LOS_NOK;
            }
            symTabHdr = (LiteSymTabHeader *)(UINTPTR)section->shOffset;
            dlInfo->liteSymTab = symTabHdr->content;
            dlInfo->symNum = symTabHdr->symTabNum;
            dlInfo->liteSymTabSize = section->shSize - sizeof(LiteSymTabHeader);
            return LOS_OK;
        }
    }
    return LOS_NOK;
}
#endif

#if (defined(LOSCFG_DYNLOAD_REL_FROM_FS) || defined(LOSCFG_DYNLOAD_REL_FROM_MEM) || \
     defined(LOSCFG_DYNLOAD_REL_FROM_MEM_STATIC))

struct ObjScnsLoadHelper {
    UINTPTR segmentsLoadAddr;
#ifdef LOSCFG_DYNLOAD_REL_FROM_MEM_STATIC
    UINTPTR codeLoadAddr;
    UINTPTR dataLoadAddr;
#endif
};

STATIC DlInfo *g_dlInfo = NULL;

LITE_OS_SEC_TEXT_MINOR UINT32 LD_ObjHdrVerify(const LD_ELF_EHDR *ehdr)
{
    if (LD_ElfHdrVerify(ehdr) != LOS_OK) {
        return LOS_NOK;
    }

    /* Check module type */
    if ((ehdr->elfType != LD_ET_REL) && (ehdr->elfType != LD_ET_EXEC)) {
        return LOS_NOK;
    }

    if (ehdr->elfShNum == 0) {
        return LOS_NOK;
    }

    /* Check the size of section */
    if (ehdr->elfShEntSize != sizeof(LD_ELF_SHDR)) {
        return LOS_NOK;
    }

    return LOS_OK;
}

STATIC UINT32 LD_ObjRelocCheckError(const LD_ELF_SHDR *relHdr)
{
    /* Some sanity checking */
    if ((relHdr->shType != LD_SHT_RELA) && (relHdr->shType != LD_SHT_REL)) {
        PRINT_ERR("reloc type error!\n");
        return LOS_NOK;
    }

    if ((relHdr->shEntSize != sizeof(LD_ELF_RELA)) && (relHdr->shEntSize != sizeof(LD_ELF_REL))) {
        PRINT_ERR("reloc entry size error!\n");
        return LOS_NOK;
    }

    return LOS_OK;
}

LITE_OS_SEC_TEXT_MINOR STATIC UINT32 LD_ObjScnsLoad(UINTPTR loadAddr, size_t availableSize, const LD_ELF_SHDR *scnHdr)
{
    if (scnHdr->shType == LD_SHT_PROGBITS) {  /* .text .rodata .data */
        if (memcpy_s((VOID *)loadAddr, availableSize, (VOID *)(UINTPTR)scnHdr->shOffset, scnHdr->shSize) != EOK) {
            goto ERR;
        }
    } else if (scnHdr->shType == LD_SHT_NOBITS) {  /* .bss */
        if (memset_s((VOID *)loadAddr, availableSize, 0, scnHdr->shSize) != EOK) {
            goto ERR;
        }
    }
    return LOS_OK;
ERR:
    return LOS_NOK;
}

STATIC INLINE BOOL IsSplitLoad(const LD_MEM_CFG_S *memCfg)
{
#ifdef LOSCFG_DYNLOAD_REL_FROM_MEM_STATIC
    if (memCfg == NULL) {
        return FALSE;
    }
    return (memCfg->regionList[0].type != LD_MEM_TYPE_ALL);
#else
    return FALSE;
#endif
}

STATIC VOID LD_ModuleMemInfoInit(DynModuleInfo *moduleHandle, const LD_MEM_CFG_S *memCfg, BOOL fromUser)
{
    /* assuming all size in moduleHandle is valid */
    if (!IsSplitLoad(memCfg)) {
        moduleHandle->segmentsAddr = memCfg->regionList[0].address;
#ifdef LOSCFG_DYNLOAD_REL_FROM_MEM_STATIC
    } else {
        /* OS always use integral load internally, need not initialize moduleHandle->segmentsAddr here */
        LOS_ASSERT(fromUser == TRUE);
        for (UINT32 idx = 0; idx < memCfg->regionNum; idx++) {
            if (memCfg->regionList[idx].type == LD_MEM_TYPE_TEXT) {
                moduleHandle->codeSegmentsAddr = memCfg->regionList[idx].address;
            } else if (memCfg->regionList[idx].type == LD_MEM_TYPE_DATA) {
                moduleHandle->dataSegmentsAddr = memCfg->regionList[idx].address;
            }
        }
#endif
    }

    moduleHandle->memFromUser = fromUser;
}

STATIC UINT32 LD_ObjScnsStoreLoadHelper(const DlInfo *dlInfo, LD_ELF_SHDR *scnHdr, struct ObjScnsLoadHelper *helper)
{
    UINT32 ret;
    UINT32 availableSize;
    UINTPTR *loadAddr = NULL;
    UINTPTR scnsLoadSize = 0;
    UINTPTR loadBaseAddr = 0;
    const DynModuleInfo *moduleHandle = dlInfo->moduleHandle;
    if (!IsSplitLoad(dlInfo->memCfg)) {
        loadAddr = &helper->segmentsLoadAddr;
        scnsLoadSize = moduleHandle->segmentsSize;
        loadBaseAddr = moduleHandle->segmentsAddr;
#ifdef LOSCFG_DYNLOAD_REL_FROM_MEM_STATIC
    } else {
        if (scnHdr->shFlags & LD_SHF_EXECINSTR) {
            loadAddr = &helper->codeLoadAddr;
            scnsLoadSize = moduleHandle->codeSegmentsSize;
            loadBaseAddr = moduleHandle->codeSegmentsAddr;
        } else {
            loadAddr = &helper->dataLoadAddr;
            scnsLoadSize = moduleHandle->dataSegmentsSize;
            loadBaseAddr = moduleHandle->dataSegmentsAddr;
        }
#endif
    }

    /* Load section content from elf to targets memory */
    availableSize = (UINT32)(loadBaseAddr + scnsLoadSize - *loadAddr);
    ret = LD_ObjScnsLoad(*loadAddr, availableSize, scnHdr);
    if (ret != LOS_OK) {
        return ret;
    }
    /* If the section attribute is allocable, the offset represents
     * the actual loading address of this section.
     */
    scnHdr->shOffset = *loadAddr;
    *loadAddr += LOS_Align(scnHdr->shSize, SECTION_ALIGN_SIZE);
    return LOS_OK;
}

LITE_OS_SEC_TEXT_MINOR UINT32 LD_ObjScnsStore(const DlInfo *dlInfo)
{
    UINT32 scnHdrIdx;
    LD_ELF_SHDR *scnHdr = NULL; /* pointer of a section descriptor */
    DynModuleInfo *moduleHandle = dlInfo->moduleHandle;
    LD_MEM_CFG_S memCfg;
    VOID *loadMem = NULL;

    if (dlInfo->memCfg == NULL) {
        loadMem = LOS_MemAllocAlign(g_dynloadHeap, (UINT32)moduleHandle->segmentsSize, DYNLOAD_ALIGN_SIZE);
        if (loadMem == NULL) {
            return LOS_NOK;
        }
        memCfg.regionNum = 1;
        memCfg.regionList[0].type = LD_MEM_TYPE_ALL;
        memCfg.regionList[0].address = (UINTPTR)loadMem;
        memCfg.regionList[0].size = (size_t)moduleHandle->segmentsSize;
        LD_ModuleMemInfoInit(moduleHandle, &memCfg, FALSE);
    } else {
        LD_ModuleMemInfoInit(moduleHandle, dlInfo->memCfg, TRUE);
    }

    struct ObjScnsLoadHelper helper = {0};
    helper.segmentsLoadAddr = moduleHandle->segmentsAddr;
#ifdef LOSCFG_DYNLOAD_REL_FROM_MEM_STATIC
    helper.codeLoadAddr = moduleHandle->codeSegmentsAddr;
    helper.dataLoadAddr = moduleHandle->dataSegmentsAddr;
#endif
    for (scnHdrIdx = 0; scnHdrIdx < dlInfo->scnNum; scnHdrIdx++) {
        scnHdr = &dlInfo->scnHdrTbl[scnHdrIdx];
        if ((scnHdr->shFlags & LD_SHF_ALLOC) && (scnHdr->shSize != 0)) {
            if (LD_ObjScnsStoreLoadHelper(dlInfo, scnHdr, &helper) != LOS_OK) {
                LD_ModuleLoadMemFree(moduleHandle);
                return LOS_NOK;
            }
        }
    }
    return LOS_OK;
}

LITE_OS_SEC_TEXT_MINOR STATIC INT32 LD_ObjScnHdrReadSetSymTab(DlInfo *dlInfo, const LD_ELF_SHDR *scnHdrTbl,
                                                              const LD_ELF_SHDR *scnHdr, UINT32 *strTblIdx)
{
    if (scnHdr->shEntSize != sizeof(LD_ELF_SYM)) {
        return LOS_NOK;
    }
    /* shLink of symbol section header is the section header index of the associated string table. */
    if (scnHdrTbl[scnHdr->shLink].shType != LD_SHT_STRTAB) {
        return LOS_NOK;
    }
    dlInfo->symTab = (LD_ELF_SYM *)(UINTPTR)scnHdr->shOffset;
    dlInfo->symNum = (UINT32)(scnHdr->shSize / scnHdr->shEntSize);
    *strTblIdx = scnHdr->shLink; /* record the index of string table section */
    return LOS_OK;
}

LITE_OS_SEC_TEXT_MINOR STATIC INT32 LD_ObjScnHdrReadCheckRel(const LD_ELF_SHDR *scnHdrTbl, const LD_ELF_SHDR *scnHdr)
{
    if (scnHdrTbl[scnHdr->shLink].shType != LD_SHT_SYMTAB) {
        return LOS_NOK;
    }
    if (LD_ObjRelocCheckError(scnHdr) != LOS_OK) {
        return LOS_NOK;
    }
    return LOS_OK;
}

LITE_OS_SEC_TEXT_MINOR STATIC INT32 LD_ObjScnHdrReadSetStrTab(DlInfo *dlInfo, const LD_ELF_SHDR *scnHdrTbl,
                                                              const LD_ELF_EHDR *ehdr, UINT32 strTblIdx)
{
    if ((strTblIdx > 0) && (strTblIdx < ehdr->elfShNum)) {
        dlInfo->strTab = (CHAR *)(UINTPTR)scnHdrTbl[strTblIdx].shOffset;
    } else {
        return LOS_NOK;
    }
    return LOS_OK;
}

STATIC size_t LD_ObjAlignSizeGet(BOOL isSplitLoad, size_t *codeSize, size_t *textSize)
{
    if (isSplitLoad) {
        *codeSize = LOS_Align(*codeSize, DYNLOAD_ALIGN_SIZE);
        *textSize = LOS_Align(*textSize, DYNLOAD_ALIGN_SIZE);
        return *codeSize + *textSize;
    } else {
        return LOS_Align(*codeSize + *textSize, DYNLOAD_ALIGN_SIZE);
    }
}

LITE_OS_SEC_TEXT_MINOR STATIC INT32 LD_ObjScnHdrReadSetDlInfo(DlInfo *dlInfo, LD_ELF_SHDR * const scnHdrTbl,
                                                              const LD_ELF_EHDR *ehdr,
                                                              const LD_MEM_REGION_S *scnsLoadSize)
{
    UINTPTR totalSize;
    DynModuleInfo *mod = dlInfo->moduleHandle;
    size_t codeSize = scnsLoadSize[LD_MEM_TYPE_TEXT].size;
    size_t dataSize = scnsLoadSize[LD_MEM_TYPE_DATA].size;

    dlInfo->scnHdrTbl = scnHdrTbl;
    dlInfo->scnNum = ehdr->elfShNum;

    totalSize = (UINTPTR)(scnsLoadSize[LD_MEM_TYPE_TEXT].size + scnsLoadSize[LD_MEM_TYPE_DATA].size);
    if (totalSize == 0) {
        PRINT_ERR("no alloc sections.\n");
        return LOS_NOK;
    }

    mod->segmentsSize = LD_ObjAlignSizeGet(IsSplitLoad(dlInfo->memCfg), &codeSize, &dataSize);
#ifdef LOSCFG_DYNLOAD_REL_FROM_MEM_STATIC
    mod->codeSegmentsSize = codeSize;
    mod->dataSegmentsSize = dataSize;
#endif
    return LOS_OK;
}

LITE_OS_SEC_TEXT_MINOR STATIC UINT32 LD_ObjScnHdrReadByType(DlInfo *dlInfo, const LD_ELF_SHDR *scnHdrTbl,
                                                            const LD_ELF_SHDR *scnHdr, LD_MEM_REGION_S *scnsLoadSize,
                                                            UINT32 *strTblIdx)
{
    switch (scnHdr->shType) {
        case LD_SHT_PROGBITS: /* text, data, literal section */
        case LD_SHT_NOBITS: /* bss */
            if (scnHdr->shFlags & LD_SHF_ALLOC) {
                if (scnHdr->shFlags & LD_SHF_EXECINSTR) {
                    scnsLoadSize[LD_MEM_TYPE_TEXT].size += LOS_Align(scnHdr->shSize, SECTION_ALIGN_SIZE);
                } else {
                    scnsLoadSize[LD_MEM_TYPE_DATA].size += LOS_Align(scnHdr->shSize, SECTION_ALIGN_SIZE);
                }
            }
            break;
        case LD_SHT_SYMTAB: /* symbol table */
            if (LD_ObjScnHdrReadSetSymTab(dlInfo, scnHdrTbl, scnHdr, strTblIdx) == LOS_NOK) {
                return LOS_NOK;
            }
            break;
        case LD_SHT_REL:
        case LD_SHT_RELA: /* relocation table */
            /* shLink of relocation section header is the section header index of the associated symbol table. */
            if (LD_ObjScnHdrReadCheckRel(scnHdrTbl, scnHdr) == LOS_NOK) {
                return LOS_NOK;
            }
            break;
        default:
            break;
    }
    return LOS_OK;
}

STATIC UINT32 LD_UserMemSizeCheck(const DlInfo *dlInfo)
{
    /* No user memory passed in, os will alloc memory later base on load size, just return success here. */
    if (dlInfo->memCfg == NULL) {
        return LOS_OK;
    }

    const LD_MEM_CFG_S *cfg = dlInfo->memCfg;
    const LD_MEM_REGION_S *region = NULL;
    size_t minSize = 0;
    UINT8 memCfgFlag[LD_MEM_TYPE_MAX] = {0};
    const DynModuleInfo *moduleHandle = dlInfo->moduleHandle;
    for (UINT32 idx = 0; idx < cfg->regionNum; idx++) {
        region = &cfg->regionList[idx];
        switch (region->type) {
            case LD_MEM_TYPE_ALL:
                minSize = moduleHandle->segmentsSize;
                break;
#ifdef LOSCFG_DYNLOAD_REL_FROM_MEM_STATIC
            case LD_MEM_TYPE_TEXT:
                minSize = moduleHandle->codeSegmentsSize;
                break;
            case LD_MEM_TYPE_DATA:
                minSize = moduleHandle->dataSegmentsSize;
                break;
#else
            case LD_MEM_TYPE_TEXT:
            case LD_MEM_TYPE_DATA:
#endif
            case LD_MEM_TYPE_MAX:
            default:
                return LOS_NOK;
        }
        memCfgFlag[region->type] = TRUE;
        if (region->size < minSize) {
            return LOS_NOK;
        }
    }

#ifdef LOSCFG_DYNLOAD_REL_FROM_MEM_STATIC
    /* check if need load text or data, but user does not provide memory. */
    if (memCfgFlag[LD_MEM_TYPE_ALL] != TRUE) {
        if ((moduleHandle->codeSegmentsSize != 0) && (memCfgFlag[LD_MEM_TYPE_TEXT] == 0)) {
            return LOS_NOK;
        }
        if ((moduleHandle->dataSegmentsSize != 0) && (memCfgFlag[LD_MEM_TYPE_DATA] == 0)) {
            return LOS_NOK;
        }
    }
#else
    (VOID)memCfgFlag;
#endif

    return LOS_OK;
}

LITE_OS_SEC_TEXT_MINOR UINT32 LD_ObjScnHdrRead(UINTPTR moduleAddr, DlInfo *dlInfo)
{
    INT32 scnHdrIdx;
    UINT32 strTblIdx = 0;
    const LD_ELF_SHDR *scnHdrTbl = NULL;
    LD_ELF_SHDR *scnHdrTblCopy = NULL;
    LD_ELF_SHDR *scnHdr = NULL; /* pointer of a section descriptor */
    LD_ELF_EHDR *ehdr = (LD_ELF_EHDR *)LD_SeekAddr(moduleAddr, 0);
    LD_MEM_REGION_S scnsLoadSize[LD_MEM_TYPE_MAX] = {};

    /* Read & check elf file header */
    dlInfo->elfHdr = ehdr;
    if (LD_ObjHdrVerify(ehdr) != LOS_OK) {
        PRINT_ERR("Obj Elf head Error!\n");
        return LOS_NOK;
    }

    /* scnHdrTblCopy will be set on dlInfo and expected to be free after reloc is done. */
    scnHdrTbl = (LD_ELF_SHDR *)LD_SeekAddr(moduleAddr, ehdr->elfShoff);
    scnHdrTblCopy = LOS_MemAlloc(g_dynloadHeap, ehdr->elfShNum * sizeof(LD_ELF_SHDR));
    if (scnHdrTblCopy == NULL) {
        return LOS_NOK;
    }
    dlInfo->scnHdrTblFromHeap = TRUE;

    (VOID)memcpy(scnHdrTblCopy, scnHdrTbl, ehdr->elfShNum * sizeof(LD_ELF_SHDR));

    for (scnHdrIdx = 0; scnHdrIdx < ehdr->elfShNum; scnHdrIdx++) {
        scnHdr = &scnHdrTblCopy[scnHdrIdx];

        if (!LD_CHECK_2_ALIGN(scnHdr->shAddrAlign)) { /* Align must be power of 2 */
            goto FAIL;
        }

        if (scnHdr->shLink >= ehdr->elfShNum) {
            PRINT_ERR("%s failed, shLink: %u\n", __FUNCTION__, scnHdr->shLink);
            goto FAIL;
        }

        /* Section memory offset */
        scnHdr->shOffset = LD_SeekAddr(moduleAddr, scnHdr->shOffset);
        if (LD_ObjScnHdrReadByType(dlInfo, scnHdrTbl, scnHdr, scnsLoadSize, &strTblIdx) != LOS_OK) {
            goto FAIL;
        }
    }

    /* get the string table */
#ifdef LOSCFG_DYNLOAD_LITE_SYMTAB
    if (LD_ObjGetLiteSymTab(dlInfo, ehdr, scnHdrTblCopy) != LOS_OK) {
        goto FAIL;
    }
#endif

    if (LD_ObjScnHdrReadSetStrTab(dlInfo, scnHdrTblCopy, ehdr, strTblIdx) == LOS_NOK) {
        goto FAIL;
    }

    if (LD_ObjScnHdrReadSetDlInfo(dlInfo, scnHdrTblCopy, ehdr, scnsLoadSize) == LOS_NOK) {
        goto FAIL;
    }

    if (LD_UserMemSizeCheck(dlInfo) != LOS_OK) {
        goto FAIL;
    }

    return LOS_OK;

FAIL:
    (VOID)LOS_MemFree(g_dynloadHeap, scnHdrTblCopy);
    dlInfo->scnHdrTbl = NULL;
    return LOS_NOK;
}

LITE_OS_SEC_TEXT_MINOR UINT32 LD_CommonManage(DynModuleInfo *moduleHandle,
                                              UINTPTR comAreaSize,
                                              const CHAR *symName,
                                              VOID **symAddr,
                                              INT8 *symType)
{
    /* If additional info is available then we are dealing with a PowerPC */
    *symType = LD_SYM_BSS | LD_SYM_COMM | LD_SYM_GLOBAL;

    /*
     * Allocate memory for new symbol. This must be done even if flag
     * LOAD_NO_SYMBOLS is applied (SPR #9259).
     */
    *symAddr = (VOID *)LOS_MemAlloc(g_dynloadHeap, (UINT32)comAreaSize);
    if (*symAddr == NULL) {
        return LOS_NOK;
    }

    if (LD_ComSymAdd(moduleHandle, (INT8 *)*symAddr) != LOS_OK) {
        PRINT_ERR("%s, %d\n", __FUNCTION__, __LINE__);
        goto LOS_ERREND;
    }

    (VOID)memset((VOID *)*symAddr, 0, comAreaSize);

    if (LD_SymAdd(moduleHandle, symName, (INT8 *)*symAddr, *symType) != LOS_OK) {
        PRINT_ERR("Can't add '%s' to symbol table.\n", symName);
        goto LOS_ERREND;
    }

    return LOS_OK;

LOS_ERREND:
    (VOID)LOS_MemFree(g_dynloadHeap, (VOID *)*symAddr);
    *symAddr = NULL;

    return LOS_NOK;
}

LITE_OS_SEC_TEXT_MINOR STATIC UINT32 LD_ObjSymTabDoProcess(const DlInfo *dlInfo, const LD_ELF_SYM *symbol,
                                                           UINT32 symIndex, INT8 *symType, const CHAR *name)
{
    VOID *addr = NULL; /* table resident symbol address */
    DynModuleInfo *moduleHandle = dlInfo->moduleHandle; /* handle of the module to be loaded */

    /*
     * Undefined external symbol or "common" symbol
     * A common symbol type is denoted by a special section index
     */
    if (symbol->stShndx == LD_SHN_COMMON) {
        if (LD_CommonManage(moduleHandle, symbol->stSize, name, &addr, symType) != LOS_OK) {
            PRINT_ERR("%s, %d\n", __FUNCTION__, __LINE__);
            return LOS_NOK;
        }
    } else {
        if (LD_SymLoadAddrGet(dlInfo, symIndex, (UINTPTR *)(VOID *)&addr) != LOS_OK) {
            return LOS_NOK;
        }

        /* Add symbol to target's symbol table */
        if (LD_SymAdd(moduleHandle, name, (INT8 *)addr, *symType) != LOS_OK) {
            PRINT_ERR("Can't add '%s' to symbol table\n", name);
            return LOS_NOK;
        }
    }
    return LOS_OK;
}

LITE_OS_SEC_TEXT_MINOR UINT32 LD_ObjSymTabProcess(const DlInfo *dlInfo)
{
    UINT32 symIndex; /* symbol index */
    CHAR *name = NULL; /* symbol name (plus EOS) */
    LD_ELF_SYM *symbol = NULL; /* current symbol being processed */
    INT8 symType; /* internal value for symbol type */

    /* Loop through all symbol table entries in object file */
    for (symIndex = 0; symIndex < dlInfo->symNum; symIndex++) {
        symbol = &dlInfo->symTab[symIndex];

        /* Determine the symbol type */
        symType = LD_ElfSymTypeGet(symbol, dlInfo->scnHdrTbl);
        name = dlInfo->strTab + symbol->stName;

        /* Determine if symbol should be put into symbol table */
        if (LD_ElfSymIsVisible(symbol)) {
            symType = (INT8)((UINT8)symType | LD_SYM_GLOBAL);
            if (LD_ObjSymTabDoProcess(dlInfo, symbol, symIndex, &symType, name) != LOS_OK) {
                return LOS_NOK;
            }
        }
    }

    return LOS_OK;
}

LITE_OS_SEC_TEXT_MINOR UINT32 LD_ObjSymTableBuild(const DlInfo *dlInfo)
{
    UINT32 symNum = dlInfo->symNum; /* number of syms in current symtab */
    DynModuleInfo *moduleHandle = dlInfo->moduleHandle; /* Handle of the module to be loaded */

#ifdef LOSCFG_DYNLOAD_LITE_SYMTAB
    return LD_ModuleSymTblCreatePreBuilt(moduleHandle, dlInfo->liteSymTab, symNum,
                                         (UINT32)dlInfo->liteSymTabSize);
#else
    CHAR *stringTable = dlInfo->strTab; /* pointer on current string table */
    LD_ELF_SYM *symTblRefs = dlInfo->symTab;

    if (!LD_ModuleSymTblCreate(moduleHandle, symTblRefs, stringTable, symNum)) {
        return LOS_NOK;
    }

    if (LD_ObjSymTabProcess(dlInfo) != LOS_OK) {
        return LOS_NOK;
    }

    if (LD_ModuleSymTblSort(moduleHandle) != LOS_OK) {
        PRINT_ERR("sort symbol tbl error!\n");
        return LOS_NOK;
    }

    return LOS_OK;
#endif
}

LITE_OS_SEC_TEXT_MINOR STATIC INT32 LD_DoObjLoad(UINTPTR moduleAddr, DlInfo *dlInfo)
{
    /* Read section header */
    if (LD_ObjScnHdrRead(moduleAddr, dlInfo) != LOS_OK) {
        return LOS_NOK;
    }

    /* load loadable sections */
    if (LD_ObjScnsStore(dlInfo) != LOS_OK) {
        return LOS_NOK;
    }

    /* Build / update the target's symbol table with symbols found in module's symbol tables. */
    if (LD_ObjSymTableBuild(dlInfo) != LOS_OK) {
        return LOS_NOK;
    }

    return LOS_OK;
}

LITE_OS_SEC_TEXT_MINOR STATIC INT32 LD_ObjArchRelocSetRelParam(const DlInfo *dlInfo, const LD_ELF_SHDR *relHdr,
                                                               const LD_ELF_RELA *relocCmd, LDRelocParam *relocParam)
{
    LD_ELF_SHDR *scnHdrTbl = dlInfo->scnHdrTbl;
    relocParam->relocType = LD_ELF_R_TYPE(relocCmd->info);
    relocParam->shType = relHdr->shType;

    /*
     * Calculate actual remote address that needs relocation,
     * and perform external or section relative relocation.
     */
    relocParam->position = scnHdrTbl[relHdr->shInfo].shOffset + relocCmd->offset;
    relocParam->addend = relocCmd->addend;
    if (LD_SymLoadAddrGet(dlInfo, LD_ELF_R_SYM(relocCmd->info), &relocParam->symAdd) != LOS_OK) {
        return LOS_NOK;
    }
    relocParam->thumbFunc = 0;

    return  LOS_OK;
}

LITE_OS_SEC_TEXT_MINOR UINT32 LD_ObjArchReloc(const DlInfo *dlInfo, const LD_ELF_SHDR *relHdr)
{
    UINTPTR relocNum = relHdr->shSize / relHdr->shEntSize; /* number of reloc entries in section */
    UINTPTR relocIdx; /* index of the reloc entry being processed */
    UINTPTR bytes = relHdr->shEntSize; /* Sizeof reloc entry */
    LD_ELF_RELA *relocCmd = NULL; /* relocation structure */
    UINT32 ret;
    LDRelocParam relocParam = {0};

    for (relocIdx = 0; relocIdx < relocNum; ++relocIdx) {
        /* Read relocation command */
        relocCmd = (LD_ELF_RELA *)((UINTPTR)relHdr->shOffset + (bytes * relocIdx));
        if (LD_ObjArchRelocSetRelParam(dlInfo, relHdr, relocCmd, &relocParam) == LOS_NOK) {
            return LOS_NOK;
        }

        ret = ArchObjRelocImp(relocCmd, &relocParam);
        if (ret != LOS_OK) {
            PRINT_ERR("Reloc error, failed to reloc type %lu\n", LD_ELF_R_TYPE(relocCmd->info));
            return LOS_NOK;
        }
    }

    return LOS_OK;
}

LITE_OS_SEC_TEXT_MINOR STATIC INT32 LD_ObjDoReloc(const DlInfo *dlInfo, const LD_ELF_SHDR *scnHdr)
{
    LD_ELF_SHDR *allocScnHdr = &dlInfo->scnHdrTbl[scnHdr->shInfo];
    if ((allocScnHdr->shFlags & LD_SHF_ALLOC) && (allocScnHdr->shType == LD_SHT_PROGBITS)) {
        if (LD_ObjArchReloc(dlInfo, scnHdr) != LOS_OK) {
            return LOS_NOK;
        }
    }
    return LOS_OK;
}

/*
 * Description: relocate the text & data section
 *
 * Input:   dlInfo - information used by relocation
 *
 * Output:  NONE.
 * RETURNS: LOS_OK- relocate succeed
 *          LOS_NOK-relocate failed
 */
LITE_OS_SEC_TEXT_MINOR INT32 LD_ObjReloc(const DlInfo *dlInfo)
{
    UINT32 scnHdrIdx;
    LD_ELF_SHDR *scnHdr = NULL; /* pointer of a section table entry */
    const DynModuleInfo *moduleHandle = dlInfo->moduleHandle;

    /* Loop through the relocation section headers */
    for (scnHdrIdx = 0; scnHdrIdx < dlInfo->scnNum; scnHdrIdx++) {
        scnHdr = &dlInfo->scnHdrTbl[scnHdrIdx];

        switch (scnHdr->shType) {
            case LD_SHT_REL:
            case LD_SHT_RELA: /* relocation table */
                if (LD_ObjDoReloc(dlInfo, scnHdr) != LOS_OK) {
                    return LOS_NOK;
                }
                break;
            default:
                break;
        }
    }

    if (!IsSplitLoad(dlInfo->memCfg)) {
        LD_FlushDCache(moduleHandle->segmentsAddr, moduleHandle->segmentsAddr + moduleHandle->segmentsSize);
#ifdef LOSCFG_DYNLOAD_REL_FROM_MEM_STATIC
    } else {
        LD_FlushDCache(moduleHandle->codeSegmentsAddr, moduleHandle->codeSegmentsAddr + moduleHandle->codeSegmentsSize);
        LD_FlushDCache(moduleHandle->dataSegmentsAddr, moduleHandle->dataSegmentsAddr + moduleHandle->dataSegmentsSize);
#endif
    }
    LD_FlushICache();
    return LOS_OK;
}

LITE_OS_SEC_TEXT_MINOR STATIC INLINE LoadFunc LD_ObjLoadFuncGet(DYN_LOAD_MODE mode)
{
#ifdef LOSCFG_DYNLOAD_REL_FROM_FS
    if (mode == LD_MODE_FS) {
        return LD_ObjFsLoad;
    }
#endif
#if defined(LOSCFG_DYNLOAD_REL_FROM_MEM) || defined(LOSCFG_DYNLOAD_REL_FROM_MEM_STATIC)
    if (mode == LD_MODE_MEM) {
        return LD_ObjMemLoad;
    }
#endif

    return NULL;
}

STATIC UINT32 LD_ObjLoadAll(const SrcInfo *srcInfo, const DynModuleInfo *moduleHandle,
                            const LoadFunc loadFunc, VOID *args)
{
    LOS_ASSERT(g_dlInfo == NULL);
    LOS_ASSERT(loadFunc != NULL);

    DlInfo *dlInfo = NULL;

    dlInfo = LD_DlAllocDlInfo(moduleHandle);
    if (dlInfo == NULL) {
        return LOS_NOK;
    }

    if (loadFunc(srcInfo, dlInfo, args) != LOS_OK) {
        LD_DlFreeDlInfo(dlInfo);
        return LOS_NOK;
    }

    g_dlInfo = dlInfo;

    return LOS_OK;
}

STATIC UINT32 LD_ObjRelocAll(const RelocFunc relocFunc)
{
    LOS_ASSERT(g_dlInfo != NULL);
    LOS_ASSERT(relocFunc != NULL);

    UINT32 ret = LOS_OK;
    DlInfo *dlInfo = g_dlInfo;

    ret = (UINT32)relocFunc(dlInfo);
    if (ret != LOS_OK) {
        goto FINISH;
    }

    SET_LOADED(dlInfo->moduleHandle);

FINISH:
    LD_DlFreeDlInfo(dlInfo);
    g_dlInfo = NULL;
    return ret;
}

LITE_OS_SEC_TEXT_MINOR VOID *LD_RelLoadModule(const SrcInfo *srcInfo, const CHAR *realPath,
                                              const LD_MEM_CFG_S *cfg, DYN_LOAD_MODE mode)
{
    DynModuleInfo *moduleHandle = NULL;

    /* Test if the module is already loaded */
    if (LD_IsModuleTaken(realPath, &moduleHandle) == TRUE) {
        if (IS_LOADED(moduleHandle)) {
            goto LOS_END;
        } else {
            goto LOS_ERREND;
        }
    }

    moduleHandle = LD_ModuleHandleAlloc(realPath);
    if (moduleHandle == NULL) {
        goto LOS_ERREND;
    }
    moduleHandle->type = ET_OBJ;

    if (LD_ObjLoadAll(srcInfo, moduleHandle, LD_ObjLoadFuncGet(mode), (VOID *)cfg) != LOS_OK) {
        goto LOS_ERRLOAD;
    }

    if (LD_ObjRelocAll(LD_ObjReloc) != LOS_OK) {
        goto LOS_ERRLOAD;
    }

    return moduleHandle;

LOS_ERRLOAD:
    (VOID)LD_ModuleUnload(moduleHandle);
LOS_ERREND:
    moduleHandle = NULL;
LOS_END:
    return moduleHandle;
}
#endif

#ifdef LOSCFG_DYNLOAD_REL_FROM_FS
LITE_OS_SEC_TEXT_MINOR UINTPTR LD_ObjReadFile2Mem(const CHAR *elfFileName)
{
    struct stat statBuff;
    SrcInfo srcInfo = { -1, NULL, 0, 0 };
    DestInfo destInfo = { NULL, 0, 0 };

    srcInfo.fd = open(elfFileName, O_RDONLY);
    if (srcInfo.fd < 0) {
        return 0;
    }

    if (stat(elfFileName, &statBuff) < 0) {
        goto LOS_ERREND;
    }
    if (statBuff.st_size > (INT_MAX - 1)) {
        goto LOS_ERREND;
    }

    destInfo.bufSize = (UINT32)statBuff.st_size;
    destInfo.needSize = (UINT32)statBuff.st_size;
    destInfo.buf = LOS_MemAlloc(g_dynloadHeap, destInfo.bufSize);
    if (destInfo.buf == NULL) {
        goto LOS_ERREND;
    }

    if (LD_DlDoRead(&srcInfo, &destInfo) != LOS_OK) {
        (VOID)LOS_MemFree(g_dynloadHeap, (VOID *)destInfo.buf);
        goto LOS_ERREND;
    }

    if (close(srcInfo.fd) < 0) {
        (VOID)LOS_MemFree(g_dynloadHeap, (VOID *)destInfo.buf);
        return 0;
    }
    return (UINTPTR)destInfo.buf;

LOS_ERREND:
    (VOID)close(srcInfo.fd);
    return 0;
}

LITE_OS_SEC_TEXT_MINOR INT32 LD_ObjFsLoad(const SrcInfo *srcInfo, DlInfo *dlInfo, const VOID *args)
{
    (VOID)srcInfo;

    DynModuleInfo *moduleHandle = dlInfo->moduleHandle;
    UINTPTR moduleAddr = LD_ObjReadFile2Mem(moduleHandle->moduleFileName);
    if (moduleAddr == 0) {
        return LOS_NOK;
    }

    dlInfo->memCfg = args;

    return LD_DoObjLoad((UINTPTR)moduleAddr, dlInfo);
}

LITE_OS_SEC_TEXT_MINOR VOID *LOS_ObjLoad(const CHAR *elfFileName)
{
    VOID *moduleHandle = NULL;
    CHAR *realPath = NULL;

    if (LD_LoadLock() != LOS_OK) {
        return NULL;
    }

    if (LD_IsLoaderInit() == FALSE) {
        if (LD_Init() != LOS_OK) {
            goto OUT;
        }
    }

    realPath = LD_FilePathGet(elfFileName);
    if (realPath == NULL) {
        PRINT_ERR("[%s] no such file: %s\n", __FUNCTION__, elfFileName);
        goto OUT;
    }

    moduleHandle = LD_RelLoadModule(NULL, realPath, NULL, LD_MODE_FS);

    (VOID)LOS_MemFree(m_aucSysMem1, realPath);

OUT:
    LD_LoadUnlock();
    return moduleHandle;
}
#endif

#if defined(LOSCFG_DYNLOAD_REL_FROM_MEM) || defined(LOSCFG_DYNLOAD_REL_FROM_MEM_STATIC)
LITE_OS_SEC_TEXT_MINOR INT32 LD_ObjMemLoad(const SrcInfo *srcInfo, DlInfo *dlInfo, const VOID *args)
{
    LOS_ASSERT(srcInfo->offset == 0);  /* offset if passed from LOS_MemLoad and should be zero */
    LOS_ASSERT(srcInfo->fileSize > 0); /* checked in LOS_MemLoad */

    dlInfo->memCfg = args;
    dlInfo->elfHdrFromUser = TRUE;

    return LD_DoObjLoad((UINTPTR)srcInfo->fileBuf, dlInfo);
}
#endif

#ifdef LOSCFG_DYNLOAD_REL_FROM_MEM_STATIC
STATIC INLINE BOOL IsLoadable(const LD_ELF_SHDR *scnHdr)
{
    return (((scnHdr->shType == LD_SHT_PROGBITS) || (scnHdr->shType == LD_SHT_NOBITS)) &&
            (scnHdr->shFlags & LD_SHF_ALLOC));
}

STATIC VOID LD_ObjSizeInfoUpdate(LD_MEM_CFG_S *cfg, size_t codeSize, size_t dataSize, size_t totalSize)
{
    LD_MEM_REGION_S *region = NULL;
    for (UINT32 i = 0; i < cfg->regionNum; i++) {
        region = &cfg->regionList[i];
        switch (region->type) {
            case LD_MEM_TYPE_ALL:
                region->size = totalSize;
                break;
            case LD_MEM_TYPE_TEXT:
                region->size = codeSize;
                break;
            case LD_MEM_TYPE_DATA:
                region->size = dataSize;
                break;
            default:
                LOS_ASSERT(FALSE);
        }
    }
}

UINT32 LD_ObjSizeGet(const CHAR *elfFileBuf, UINT32 bufLen, LD_MEM_CFG_S *cfg)
{
    LOS_ASSERT(cfg != NULL);
    LOS_ASSERT(cfg->regionNum <= LD_MEM_TYPE_MAX);

    size_t dataSize = 0;
    size_t codeSize = 0;
    size_t totalSize = 0;
    const LD_ELF_SHDR *scnHdr = NULL;
    const LD_ELF_EHDR *ehdr = (LD_ELF_EHDR *)LD_SeekAddr((UINTPTR)elfFileBuf, 0);
    const LD_ELF_SHDR *scnHdrTbl = (LD_ELF_SHDR *)LD_SeekAddr((UINTPTR)elfFileBuf, ehdr->elfShoff);
    if (LD_ObjHdrVerify(ehdr) != LOS_OK) {
        return LOS_ERRNO_LD_INVALID_ELF;
    }

    /* Calculate sizes via section header table */
    for (UINT32 scnHdrIdx = 0; scnHdrIdx < ehdr->elfShNum; scnHdrIdx++) {
        scnHdr = &scnHdrTbl[scnHdrIdx];
        if (IsLoadable(scnHdr)) {
            if (scnHdr->shFlags & LD_SHF_EXECINSTR) {
                codeSize += LOS_Align(scnHdr->shSize, SECTION_ALIGN_SIZE);
            } else {
                dataSize += LOS_Align(scnHdr->shSize, SECTION_ALIGN_SIZE);
            }
        }
    }
    totalSize = LD_ObjAlignSizeGet(IsSplitLoad(cfg), &codeSize, &dataSize);

    /* save result to user config structure */
    LD_ObjSizeInfoUpdate(cfg, codeSize, dataSize, totalSize);

    return LOS_OK;
}
#endif /* LOSCFG_DYNLOAD_REL_FROM_MEM_STATIC */

#endif /* LOSCFG_KERNEL_DYNLOAD_REL */
