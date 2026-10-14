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

#include "los_ld_symlib_pri.h"
#include "los_ld_commlib_pri.h"
#include "los_ld_modulelib_pri.h"
#include "los_ld_macro_pri.h"

#include "los_memory.h"
#include "los_hwi.h"

LITE_OS_SEC_TEXT_MINOR BOOL LD_ModuleSymTblAlloc(DynModuleInfo *moduleHandle, UINT32 symTotalNum, UINT32 strTotalLen)
{
    SymbolManage *symManage = &moduleHandle->symManage;

    if ((symTotalNum == 0) && (strTotalLen == 0)) {
        return TRUE;
    } else if ((symTotalNum == 0) || (strTotalLen == 0)) {
        return FALSE;
    }

    if ((symManage->symTbl != NULL) || (symManage->strTbl != NULL)) {
        PRINT_ERR("SymTbl or StrTbl is not NULL before alloc!\n");
        return FALSE;
    }

    symManage->symTbl = (LDSymbol *)LOS_MemAlloc(g_dynloadHeap,
                                                 (UINT32)(symTotalNum * sizeof(LDSymbol) + strTotalLen));
    if (symManage->symTbl == NULL) {
        return FALSE;
    }
    symManage->strTbl = (CHAR *)(symManage->symTbl + symTotalNum);
    symManage->symTotalNum = symTotalNum;
    symManage->strTotalLen = strTotalLen;

    return TRUE;
}

#ifdef LOSCFG_DYNLOAD_LITE_SYMTAB
UINT32 LD_ModuleSymTblCreatePreBuilt(DynModuleInfo *moduleHandle, CHAR *symTbl, UINT32 symNum, UINT32 size)
{
    SymbolManage *symManage = &moduleHandle->symManage;
    UINT32 strSize = size - symNum * (UINT32)sizeof(LDSymbol);

    if (symNum >= (UINT32)(INT32_MAX / sizeof(LDSymbol))) {
        return LOS_NOK;
    }
    if (!LD_ModuleSymTblAlloc(moduleHandle, symNum, strSize)) {
        return LOS_NOK;
    }

    (VOID)memcpy((VOID *)symManage->symTbl, (VOID *)symTbl, (size_t)size);

    for (UINT32 idx = 0; idx < symNum; idx++) {
        symManage->symTbl[idx].name += (UINTPTR)symManage->strTbl;
        symManage->symTbl[idx].addr += moduleHandle->segmentsAddr;
    }
    symManage->strLen = symManage->strTotalLen;
    symManage->symNum = symManage->symTotalNum;
    return LOS_OK;
}
#endif

LITE_OS_SEC_TEXT_MINOR BOOL LD_ModuleSymTblCreate(DynModuleInfo *moduleHandle, const LD_ELF_SYM *symTbl,
                                                  const CHAR *strTbl, UINT32 symNum)
{
    UINT32 idx;
    UINT32 symTotalNum = 0;
    size_t strTotalLen = 0;
    const LD_ELF_SYM *symbol = NULL;
    const CHAR *name = NULL;

    /* Loop thru all symbol table entries in object file. */
    for (idx = 0; idx < symNum; idx++) {
        symbol = symTbl + idx;
        name = strTbl + symbol->stName;

        /* Determine if symbol should be put into symbol table. */
        if (LD_ElfSymIsVisible(symbol)) {
            symTotalNum += 1;
            strTotalLen += strlen(name) + 1;
        }
    }

    return LD_ModuleSymTblAlloc(moduleHandle, symTotalNum, (UINT32)strTotalLen);
}

LITE_OS_SEC_TEXT_MINOR STATIC const LDSymbol *LD_DoSymSearchByName(const LDSymbol *searchArray, const CHAR *name,
                                                                   INT32 startIdx, INT32 endIdx)
{
    INT32 mid;
    INT32 ret;

    while (startIdx <= endIdx) {
        mid = startIdx + (INT32)((UINT32)(endIdx - startIdx) >> 1); /* Binary search */
        ret = strcmp(searchArray[mid].name, name);
        if (ret > 0) {
            endIdx = mid - 1;
        } else if (ret < 0) {
            startIdx = mid + 1;
        } else {
            return &searchArray[mid];
        }
    }
    return NULL;
}

LITE_OS_SEC_TEXT_MINOR STATIC const LDSymbol *LD_SymSearchByName(const DynModuleInfo *moduleHandle, const CHAR *name)
{
    LDSymbol *searchArray = moduleHandle->symManage.symTbl;
    UINT32 searchNum = moduleHandle->symManage.symNum;
    if (searchNum == 0) {
        return NULL;
    }

    return LD_DoSymSearchByName(searchArray, name, 0, (INT32)(searchNum - 1));
}

LITE_OS_SEC_TEXT_MINOR STATIC VOID LD_DoModuleSymTblSort(LDSymbol *sortArray, UINT32 start, UINT32 end)
{
    UINT32 left = start;
    UINT32 right = end;
    UINT32 idx = start;
    LDSymbol pivot = sortArray[start];

    while (left < right) {
        while ((left < right) && (strcmp(sortArray[right].name, pivot.name) > 0)) {
            right--;
        }

        if (left < right) {
            sortArray[left] = sortArray[right];
            idx = right;
            left++;
        }

        while ((left < right) && (strcmp(sortArray[left].name, pivot.name) < 0)) {
            left++;
        }

        if (left < right) {
            sortArray[right] = sortArray[left];
            idx = left;
            right--;
        }
    }

    sortArray[idx] = pivot;

    if (start < idx) {
        LD_DoModuleSymTblSort(sortArray, start, idx - 1);
    }
    if (idx < end) {
        LD_DoModuleSymTblSort(sortArray, idx + 1, end);
    }
}

LITE_OS_SEC_TEXT_MINOR UINT32 LD_ModuleSymTblSort(const DynModuleInfo *moduleHandle)
{
    UINT32 strCurrLen = moduleHandle->symManage.strLen;
    UINT32 strTotalLen = moduleHandle->symManage.strTotalLen;
    UINT32 symCurrNum = moduleHandle->symManage.symNum;
    UINT32 symTotalNum = moduleHandle->symManage.symTotalNum;
    LDSymbol *sortArray = moduleHandle->symManage.symTbl;
    if ((symTotalNum != symCurrNum) || (strCurrLen != strTotalLen)) {
        return LOS_NOK;
    }
    if (symTotalNum == 0) {
        return LOS_OK;
    }

    LD_DoModuleSymTblSort(sortArray, 0, symTotalNum - 1);

    return LOS_OK;
}

LITE_OS_SEC_TEXT_MINOR UINT32 LD_ComSymAdd(DynModuleInfo *moduleHandle, const INT8 *value)
{
    UINT32 intSave;
    DynComSymInfo *comSymInfo = (DynComSymInfo *)LOS_MemAlloc(g_dynloadHeap, sizeof(DynComSymInfo));
    if (comSymInfo == NULL) {
        return LOS_NOK;
    }
    comSymInfo->symAddr = (UINTPTR)value;
    intSave = LOS_IntLock();
    LOS_ListAdd(&moduleHandle->comSymInfoList, &comSymInfo->linkNode);
    LOS_IntRestore(intSave);

    return LOS_OK;
}

LITE_OS_SEC_TEXT_MINOR UINT32 LD_SymAdd(DynModuleInfo *moduleHandle, const CHAR *name, const INT8 *addr, INT8 type)
{
    (VOID)type;
    LDSymbol *symTbl = NULL;
    CHAR *strTbl = NULL;
    SymbolManage *symManage = NULL;
    errno_t err;

    symManage = &moduleHandle->symManage;
    /* check if already alloc the symbol table and string table of this module. */
    if ((symManage->symTotalNum == 0) || (symManage->strTotalLen == 0)) {
        PRINT_ERR("symTotalNum and strTotalLen is error!\n");
        return LOS_NOK;
    }

    /* check if exist enough space to store the new symbol. */
    if ((symManage->symTotalNum <= symManage->symNum) ||
        (symManage->strTotalLen < (symManage->strLen + strlen(name) + 1))) {
        PRINT_ERR("no enough space to store a new symbol.\n");
        return LOS_NOK;
    }

    symTbl = symManage->symTbl;
    strTbl = symManage->strTbl;

    /* append to the end of string table and insert in the symbol table. */
    err = strncpy_s(&strTbl[symManage->strLen], symManage->strTotalLen - symManage->strLen,
                    name, strlen(name));
    if (err != EOK) {
        PRINT_ERR("%s failed, errno = %d\n", __func__, err);
        return LOS_NOK;
    }

    symTbl[symManage->symNum].name = &strTbl[symManage->strLen];
    symTbl[symManage->symNum].addr = addr;
    symManage->strLen += (UINT32)(strlen(name) + 1);
    symManage->symNum += 1;

    return LOS_OK;
}

#ifdef LOSCFG_DYNLOAD_NO_SYMBOLS_FAULT_TOLERANCE
UINT32 LD_SymDefaultLoadAddr(VOID)
{
    return 0xFFFFFFFF;
}
#endif

LITE_OS_SEC_TEXT_MINOR STATIC UINT32 LD_SymLoadAddrDoGet(const DlInfo *dlInfo, const LD_ELF_SYM *symbol,
                                                         const CHAR *name, UINTPTR *loadAddrTmp)
{
    *loadAddrTmp = (UINTPTR)LD_FindSymInDepended(dlInfo, name);
    if (*loadAddrTmp == 0) {
        /* We only allow an undefined symbol if this is a weak reference */
        if (LD_ELF_ST_BIND(symbol->stInfo) != LD_STB_WEAK) {
#ifdef LOSCFG_DYNLOAD_NO_SYMBOLS_FAULT_TOLERANCE
            *loadAddrTmp = (UINTPTR)LD_SymDefaultLoadAddr;
#else
            PRINT_ERR("cannot get symbol address \"%s\"\n", name);
            return LOS_NOK;
#endif
        }
    }
    return LOS_OK;
}

LITE_OS_SEC_TEXT_MINOR UINT32 LD_SymLoadAddrGet(const DlInfo *dlInfo, UINT32 symIndex, UINTPTR *loadAddr)
{
    LD_ELF_SYM *symbol = &dlInfo->symTab[symIndex];
    UINTPTR bias = 0; /* section relative address */
    CHAR *name = dlInfo->strTab + symbol->stName; /* symbol name (plus EOS) */
    UINTPTR loadAddrTmp = 0;
    enum ElfType elfType = dlInfo->moduleHandle->type;

    /*
     * Symbol table entry of index 0 is reserved by elf standard.
     * No type, local binding, no section, no size, no name.
     * if the symbol table index is 0 of a relocation, means not care about the symbol load address.
     */
    if (symIndex == 0) {
        if ((LD_ELF_ST_BIND(symbol->stInfo) != LD_STB_LOCAL) ||
            (LD_ELF_ST_TYPE(symbol->stInfo) != LD_STT_NOTYPE)) {
            PRINT_ERR("index 0 of the symbol table should be reserved!\n");
            return LOS_NOK;
        }
        return LOS_OK;
    }

    /* Determine the type of processing based on the symbol type */
    if ((symbol->stShndx == LD_SHN_UNDEF) || (symbol->stShndx == LD_SHN_COMMON)) {
        /* Look up undefined external symbol in symbol table */
        if (symbol->stName != 0) {
            if (LD_SymLoadAddrDoGet(dlInfo, symbol, name, &loadAddrTmp) != LOS_OK) {
                return LOS_NOK;
            }
        }
    } else {
        /* Bias is not needed either when symbol is absolute */
        if (elfType == LD_ET_REL) {
            if (symbol->stShndx == LD_SHN_ABS) {
                bias = 0;
            } else {
                /* Compute the bias. In ELF is it the base address of the section when loaded */
                bias = dlInfo->scnHdrTbl[symbol->stShndx].shOffset;
            }
        } else if (elfType == LD_ET_EXEC) {
            bias = 0;
        } else if (elfType == LD_ET_DYN) {
            if (dlInfo->memCfg == NULL || dlInfo->memCfg->regionList[0].type == LD_MEM_TYPE_ALL) {
                bias = dlInfo->loadBias;
#if defined(LOSCFG_DYNLOAD_DYN_FROM_MEM_STATIC) || defined(LOSCFG_DYNLOAD_DYN_FROM_FS_STATIC)
            } else {
                if ((symbol->stInfo & LD_STT_OBJECT) != 0) {
                    bias = dlInfo->loadDataBias;
                } else {
                    bias = dlInfo->loadCodeBias;
                }
#endif
            }
        }
        loadAddrTmp = symbol->stValue + bias;
    }

    *loadAddr = loadAddrTmp;
    return LOS_OK;
}

LITE_OS_SEC_TEXT_MINOR STATIC const LDSymbol *LD_FindInModule(DynModuleInfo *handle, const CHAR *name)
{
    const LDSymbol *sym = NULL;
    DynModuleInfo *moduleHandle = handle;

    /* no need repeat search the same symbol in a same module twice. */
    if (IS_SEARCHING(moduleHandle)) {
        return NULL;
    }
    /* this module had been searched. */
    SET_SEARCHING(moduleHandle);

    sym = LD_SymSearchByName(moduleHandle, name);
    if (sym != NULL) {
        return sym;
    }

#if defined(LOSCFG_DYNLOAD_DYN_FROM_MEM) || defined(LOSCFG_DYNLOAD_DYN_FROM_FS) || \
    defined(LOSCFG_DYNLOAD_DYN_FROM_MEM_STATIC) || defined(LOSCFG_DYNLOAD_DYN_FROM_FS_STATIC)
    if (moduleHandle->type == ET_SO) {
        for (UINT32 idx = 0; idx < N_MAX_MODULES; ++idx) {
            if (moduleHandle->apDlNeeded[idx] == NULL) {
                break;
            }

            sym = LD_FindInModule(moduleHandle->apDlNeeded[idx], name);
            if (sym != NULL) {
                return sym;
            }
        }
    }
#endif

    return NULL;
}

typedef const LDSymbol *(*LD_SYM_FINDER)(DynModuleInfo *moduleHandle, UINTPTR arg);

LITE_OS_SEC_TEXT_MINOR STATIC const LDSymbol *LD_FindInSymtblHelper(UINT32 startIdx, LD_SYM_FINDER finder,
                                                                    UINTPTR find, UINT32 *moduleIdx)
{
    const LDSymbol *sym = NULL;
    const SymbolManage *symManage = NULL;
    DynModuleInfo *moduleHandle = NULL;

    LOS_ASSERT(moduleIdx != NULL);

    for (UINT32 idx = startIdx; idx < N_MAX_MODULES; ++idx) {
        moduleHandle = GET_MODULE_BY_ID(idx);
        symManage = &moduleHandle->symManage;
        if ((!IS_TAKEN(moduleHandle)) ||
            (IS_SEARCHING(moduleHandle)) ||
            (symManage->symNum == 0)) {
            continue;
        }

        sym = finder(moduleHandle, find);
        if (sym != NULL) {
            *moduleIdx = idx;
            return sym;
        }
    }
    return NULL;
}

LITE_OS_SEC_TEXT_MINOR STATIC const LDSymbol *LD_FindInSymtblComm(LD_SYM_FINDER finder, UINTPTR find, BOOL bCheckRedef,
                                                                  UINT32 *moduleIdx)
{
    UINT32 idxSave;
    UINT32 dupIdx;
    const LDSymbol *symSave = NULL;
    const LDSymbol *dupSym = NULL;

    LOS_ASSERT(finder != NULL);
    LOS_ASSERT(moduleIdx != NULL);

    symSave = LD_FindInSymtblHelper(0, finder, find, &idxSave);
    if (symSave == NULL) {
        return NULL;
    }

    /* find duplicated symbol if exist */
    if (bCheckRedef) {
        dupSym = LD_FindInSymtblHelper(idxSave + 1, finder, find, &dupIdx);
        if (dupSym != NULL) {
            PRINT_ERR("\"%s\" redefined : [\"%s\", \"%s\"]!\n",
                      symSave->name,
                      GET_MODULE_BY_ID(dupIdx)->moduleFileName,
                      GET_MODULE_BY_ID(idxSave)->moduleFileName);
            return NULL;
        }
    }

    *moduleIdx = idxSave;
    return symSave;
}

LITE_OS_SEC_TEXT_MINOR STATIC const LDSymbol *LD_FindNameAdaptor(DynModuleInfo *moduleHandle, UINTPTR arg)
{
    return LD_SymSearchByName(moduleHandle, (const CHAR *)arg);
}

LITE_OS_SEC_TEXT_MINOR STATIC const LDSymbol *LD_FindInSymtbl(const CHAR *name, BOOL bCheckRedef)
{
    UINT32 moduleIdx;
    return LD_FindInSymtblComm(LD_FindNameAdaptor, (UINTPTR)name, bCheckRedef, &moduleIdx);
}

LITE_OS_SEC_TEXT_MINOR STATIC INLINE BOOL InRange(UINTPTR addr, UINTPTR start, UINTPTR len)
{
    return ((addr >= start) && (addr < (start + len)));
}

LITE_OS_SEC_TEXT_MINOR STATIC INLINE BOOL InModuleVA(DynModuleInfo *moduleHandle, UINTPTR addr)
{
#if defined(LOSCFG_DYNLOAD_REL_FROM_MEM_STATIC) || \
    defined(LOSCFG_DYNLOAD_DYN_FROM_MEM_STATIC) || \
    defined(LOSCFG_DYNLOAD_DYN_FROM_FS_STATIC)
    return InRange(addr, moduleHandle->dataSegmentsAddr, moduleHandle->dataSegmentsSize) ||
           InRange(addr, moduleHandle->codeSegmentsAddr, moduleHandle->codeSegmentsSize);
#else
    return InRange(addr, moduleHandle->segmentsAddr, moduleHandle->segmentsSize);
#endif
}

LITE_OS_SEC_TEXT_MINOR STATIC const LDSymbol *LD_FindNearestSymbol(DynModuleInfo *moduleHandle, UINTPTR arg)
{
    UINTPTR addr = arg;
    UINTPTR minGap = UINTMAX;
    LDSymbol *nearest = NULL;
    UINTPTR symAddr;

    LOS_ASSERT(moduleHandle != NULL);

    if (!InModuleVA(moduleHandle, addr)) {
        return NULL;
    }

    /* walk through symbol table to found the neareset symbol */
    const SymbolManage *symMgr = &moduleHandle->symManage;
    for (UINT32 idx = 0; idx < symMgr->symNum; idx++) {
        symAddr = (UINTPTR)symMgr->symTbl[idx].addr;
        if (addr < symAddr) {
            continue;
        }
        if ((addr - symAddr) < minGap) {
            minGap = addr - symAddr;
            nearest = &symMgr->symTbl[idx];
            if (minGap == 0) {
                break;
            }
        }
    }
    return nearest;
}

typedef struct LDSymbolAux {
    const LDSymbol *sym;
    DynModuleInfo *moduleHandle;
} LDSymbolAux;

LITE_OS_SEC_TEXT_MINOR STATIC UINT32 LD_FindInSymtblByAddr(UINTPTR address, BOOL bCheckRedef, LDSymbolAux *symAux)
{
    UINT32 moduleIdx = 0;
    const LDSymbol *sym = LD_FindInSymtblComm(LD_FindNearestSymbol, address, bCheckRedef, &moduleIdx);
    if (sym == NULL) {
        return LOS_NOK;
    }

    symAux->sym = sym;
    symAux->moduleHandle = GET_MODULE_BY_ID(moduleIdx);
    return LOS_OK;
}

#if defined(LOSCFG_DYNLOAD_DYN_FROM_MEM) || defined(LOSCFG_DYNLOAD_DYN_FROM_FS) || \
    defined(LOSCFG_DYNLOAD_DYN_FROM_MEM_STATIC) || defined(LOSCFG_DYNLOAD_DYN_FROM_FS_STATIC)
LITE_OS_SEC_TEXT_MINOR STATIC UINT64 LD_NameHashValGet(const CHAR *name)
{
    UINT64 hashVal = 0;
    UINT64 tmp;
    const UINT8 *str = (const UINT8 *)name;

    while (*str) {
        /* 4, 0xf0000000U: Standard calculation method of hash value for symbol name in shared object */
        hashVal = (hashVal << 4) + (UINT64)(*str);
        tmp = hashVal & 0xf0000000U;
        hashVal ^= tmp;
        /* 24: Standard calculation method of hash value for symbol name in shared object */
        hashVal ^= tmp >> 24;
        str++;
    }

    return hashVal;
}
#endif

#if defined(LOSCFG_DYNLOAD_DYN_FROM_MEM) || defined(LOSCFG_DYNLOAD_DYN_FROM_FS) || \
    defined(LOSCFG_DYNLOAD_DYN_FROM_MEM_STATIC) || defined(LOSCFG_DYNLOAD_DYN_FROM_FS_STATIC)
LITE_OS_SEC_TEXT_MINOR STATIC VOID *LD_FindInModuleByHashInfo(const DynModuleInfo *moduleHandle, const CHAR *name)
{
    LD_ELF_SYM *symbol = NULL;
    CHAR *curName = NULL;
    UINT64 hashVal;
    UINT32 i;

    if ((moduleHandle->type != ET_SO) || (moduleHandle->hashInfo.bucketSize == 0)) {
        return NULL;
    }

    hashVal = LD_NameHashValGet(name);
    for (i = moduleHandle->hashInfo.bucket[hashVal % moduleHandle->hashInfo.bucketSize];
         (i != 0) && (i < moduleHandle->hashInfo.chainSize); i = moduleHandle->hashInfo.chain[i]) {
        symbol = &moduleHandle->symTab[i];
        curName = moduleHandle->strTab + symbol->stName;
        if (((curName != NULL) && (strcmp(curName, name) == 0)) && (symbol->stShndx != LD_SHN_UNDEF) &&
            ((LD_ELF_ST_BIND(symbol->stInfo) == LD_STB_GLOBAL) || (LD_ELF_ST_BIND(symbol->stInfo) == LD_STB_WEAK))) {
#if defined(LOSCFG_DYNLOAD_DYN_FROM_MEM_STATIC) || defined(LOSCFG_DYNLOAD_DYN_FROM_FS_STATIC)
            if ((symbol->stInfo & LD_STT_OBJECT) != 0) {
                return (VOID *)(symbol->stValue + moduleHandle->dataSegmentsAddr);
            } else {
                return (VOID *)(symbol->stValue + moduleHandle->codeSegmentsAddr);
            }
#else
            return (VOID *)(symbol->stValue + moduleHandle->segmentsAddr);
#endif
        }
    }
    return NULL;
}
#endif

LITE_OS_SEC_TEXT_MINOR VOID *LD_FindSymInDepended(const DlInfo *dlInfo, const CHAR *name)
{
    const LDSymbol *symbol = NULL;
    DynModuleInfo *moduleHandle = dlInfo->moduleHandle;

    if (name == NULL) {
        return NULL;
    }

    if (moduleHandle != NULL) {
#if defined(LOSCFG_DYNLOAD_DYN_FROM_MEM) || defined(LOSCFG_DYNLOAD_DYN_FROM_FS) || \
    defined(LOSCFG_DYNLOAD_DYN_FROM_MEM_STATIC) || defined(LOSCFG_DYNLOAD_DYN_FROM_FS_STATIC)
        VOID *addr = NULL;
        if ((moduleHandle->hashInfo.bucket != NULL) && (moduleHandle->hashInfo.chain != NULL)) {
            addr = LD_FindInModuleByHashInfo(moduleHandle, name);
            if (addr != NULL) {
                return addr;
            }
        } else {
#endif
            symbol = LD_FindInModule(moduleHandle, name);
#if defined(LOSCFG_DYNLOAD_DYN_FROM_MEM) || defined(LOSCFG_DYNLOAD_DYN_FROM_FS) || \
    defined(LOSCFG_DYNLOAD_DYN_FROM_MEM_STATIC) || defined(LOSCFG_DYNLOAD_DYN_FROM_FS_STATIC)
        }
#endif
    }

    if (symbol == NULL) {
        symbol = LD_FindInSymtbl(name, TRUE);
    }

    /* clear the searching flags. */
    LD_SearchFlagsClear();
    return (symbol != NULL) ? (VOID *)symbol->addr : NULL;
}

LITE_OS_SEC_TEXT_MINOR INT32 LD_SymRemoveByModuleHandle(DynModuleInfo *moduleHandle)
{
    /* the system symbol module's string table is NULL. */
    if ((moduleHandle->symManage.symTbl != NULL) && (moduleHandle->symManage.strTbl != NULL)) {
        (VOID)LOS_MemFree(g_dynloadHeap, moduleHandle->symManage.symTbl);
        moduleHandle->symManage.symTbl = NULL;
    }

#ifndef LOSCFG_DYNLOAD_LITE_SYMTAB
    DynComSymInfo *curComSymInfo = NULL;
    while (!LOS_ListEmpty(&moduleHandle->comSymInfoList)) {
        curComSymInfo = LOS_DL_LIST_ENTRY(LOS_DL_LIST_FIRST(&moduleHandle->comSymInfoList), DynComSymInfo, linkNode);

        /* Remove from module info */
        LOS_ListDelete(&curComSymInfo->linkNode);

        if (LOS_MemFree(g_dynloadHeap, (VOID *)curComSymInfo->symAddr) != LOS_OK) {
            return LOS_NOK;
        }
        curComSymInfo->symAddr = 0;

        if (LOS_MemFree(g_dynloadHeap, curComSymInfo) != LOS_OK) {
            return LOS_NOK;
        }
    }
#endif

    return LOS_OK;
}

LITE_OS_SEC_TEXT_MINOR UINT32 LD_FindSymByAddr(VOID *handle, UINTPTR addr, LDSymbolInfo *symInfo)
{
    errno_t err;
    UINT32 ret = LOS_OK;
    const LDSymbol *symbol = NULL;
    DynModuleInfo *moduleHandle = (DynModuleInfo *)handle;
    LDSymbolAux symAux = {NULL};

    if ((addr == (UINTPTR)NULL) || (symInfo == NULL)) {
        return LOS_NOK;
    }

    if (LD_LoadLock() != LOS_OK) {
        return LOS_NOK;
    }

    if (LD_IsLoaderInit() == FALSE) {
        ret = LOS_NOK;
        goto UNLOCK;
    }

    if (moduleHandle == NULL) {
        ret = LD_FindInSymtblByAddr(addr, FALSE, &symAux);
        if (ret != LOS_OK) {
            goto CLEAR_FLAGS;
        }
    } else {
        symbol = LD_FindNearestSymbol(moduleHandle, addr);
        if (symbol == NULL) {
            ret = LOS_NOK;
            goto CLEAR_FLAGS;
        }
        symAux.moduleHandle = moduleHandle;
        symAux.sym = symbol;
    }

    err = strcpy_s(symInfo->moduleName, sizeof(symInfo->moduleName), symAux.moduleHandle->moduleFileName);
    if (err != EOK) {
        ret = LOS_NOK;
        goto CLEAR_FLAGS;
    }
    err = strcpy_s(symInfo->symName, sizeof(symInfo->symName), symAux.sym->name);
    if (err != EOK) {
        ret = LOS_NOK;
        goto CLEAR_FLAGS;
    }
    symInfo->symAddr = (UINTPTR)symAux.sym->addr;

CLEAR_FLAGS:
    LD_SearchFlagsClear();
UNLOCK:
    LD_LoadUnlock();

    return ret;
}

LITE_OS_SEC_TEXT_MINOR VOID *LOS_FindSymByName(VOID *handle, CHAR *name)
{
    const LDSymbol *symbol = NULL;
    DynModuleInfo *moduleHandle = NULL;
    VOID *addr = NULL;

    if (name == NULL) {
        return NULL;
    }

    if (LD_LoadLock() != LOS_OK) {
        return NULL;
    }

    if (LD_IsLoaderInit() == FALSE) {
        PRINT_ERR("please load first!\n");
        goto OUT;
    }

    moduleHandle = (DynModuleInfo *)handle;
    if (moduleHandle == NULL) {
        symbol = LD_FindInSymtbl(name, FALSE);
    } else {
#if defined(LOSCFG_DYNLOAD_DYN_FROM_MEM) || defined(LOSCFG_DYNLOAD_DYN_FROM_FS) || \
    defined(LOSCFG_DYNLOAD_DYN_FROM_MEM_STATIC) || defined(LOSCFG_DYNLOAD_DYN_FROM_FS_STATIC)
        if ((moduleHandle->hashInfo.bucket != NULL) && (moduleHandle->hashInfo.chain != NULL)) {
            addr = LD_FindInModuleByHashInfo(moduleHandle, name);
            if (addr != NULL) {
                goto OUT;
            }
        } else {
#endif
            symbol = LD_FindInModule(moduleHandle, name);
#if defined(LOSCFG_DYNLOAD_DYN_FROM_MEM) || defined(LOSCFG_DYNLOAD_DYN_FROM_FS) || \
    defined(LOSCFG_DYNLOAD_DYN_FROM_MEM_STATIC) || defined(LOSCFG_DYNLOAD_DYN_FROM_FS_STATIC)
        }
#endif
    }

    /* clear the searching flags. */
    LD_SearchFlagsClear();
    addr = (symbol != NULL) ? (VOID *)symbol->addr : NULL;

OUT:
    LD_LoadUnlock();
    return addr;
}
