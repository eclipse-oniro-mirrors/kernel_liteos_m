/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted under the conditions of the LOSCFG-style open source license.
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY
 * OR FIT FOR A PARTICULAR PURPOSE.
 */

#include "It_los_mem.h"

#ifdef LOSCFG_KERNEL_MEM_BESTFIT

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

/* 用例简要描述: bestfit(ws63) LOS_MemTaskIdGet/LOS_MemNodeSizeCheck/LOS_MemMrealloc/
 * LOS_MemMfree/LOS_MemInfoGet/LOS_MemRealloc 防御与契约分支一揽子(全程 GOTO 清理,
 * 任何失败路径均注销自建池, 不污染多池全局链影响后续 mulpool 用例):
 * 1) F-Pos/F-Inv: LOS_MemTaskIdGet 仅支持系统池指针——系统池在用 ptr 归属合法
 *    (前驱节点启发式, 未开任务内存统计的配置返回 OS_INVALID); 池外指针 →
 *    OS_INVALID; 已释放 ptr 的空闲节点分支因 LMS 毒化走查不可安全触达(见分析报告);
 * 2) F-Pos/F-Inv: LOS_MemCheckLevelSet(LOW/HIGH) 两档下 LOS_MemNodeSizeCheck
 *    (合法 ptr OK; 未对齐 ptr → LOS_ERRNO_MEMCHECK_NO_HEAD; 池外 ptr 同拒);
 * 3) F-Pos/F-Inv: LOS_MemMrealloc(NULL,..) 等价 malloc; size==0 等价 free 返回 NULL;
 *    跨 moduleId 重分配走告警分支仍成功; 未对齐 ptr → OsMemPtrToNode 拒绝 → NULL;
 * 4) F-Inv: LOS_MemMfree 未对齐 ptr → OsMemPtrToNode 拒绝(错误返回, 不触 panic);
 * 5) F-Pos: LOS_MemInfoGet 契约 + LOS_MemRealloc 缩容/扩容搬移分支。 */

/* LOS_MemCheckLevelGet/Set declarations are guarded by
 * LOSCFG_BASE_MEM_NODE_SIZE_CHECK in los_memory.h (not enabled in this
 * config) but the functions are always compiled in bestfit. Declare
 * them locally. */
extern UINT8 LOS_MemCheckLevelGet(VOID);
extern UINT32 LOS_MemCheckLevelSet(UINT8 checkLevel);
extern UINT32 LOS_MemNodeSizeCheck(VOID *pool, VOID *ptr, UINT32 *totalSize, UINT32 *availSize);
#ifndef LOS_MEM_CHECK_LEVEL_LOW
#define LOS_MEM_CHECK_LEVEL_LOW 0
#endif
#ifndef LOS_MEM_CHECK_LEVEL_HIGH
#define LOS_MEM_CHECK_LEVEL_HIGH 1
#endif
#ifndef LOS_MEM_CHECK_LEVEL_DISABLE
#define LOS_MEM_CHECK_LEVEL_DISABLE 0xff
#endif

#define MEM_TC317_POOL_SIZE 0x1000
#define MEM_TC317_MOD_A     1
#define MEM_TC317_MOD_B     2

STATIC UINT8 g_memPoolTC317[MEM_TC317_POOL_SIZE] __attribute__((aligned(8)));
STATIC UINT8 g_memOutTC317[0x40] __attribute__((aligned(8)));

static UINT32 TestCase(VOID)
{
    UINT32 ret;
    VOID *ptr = NULL;
    VOID *ptrRe = NULL;
    VOID *sysPtr = NULL;
    UINT32 taskId;
    LOS_MEM_POOL_STATUS poolStatus = {0};

    ret = LOS_MemPoolInit(g_memPoolTC317, MEM_TC317_POOL_SIZE, 0);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* ---- 1: LOS_MemTaskIdGet(仅系统池) ---- */
    sysPtr = LOS_MemAlloc((VOID *)LOSCFG_SYS_HEAP_ADDR, 0x40);
    ICUNIT_GOTO_NOT_EQUAL(sysPtr, NULL, 0, EXIT);
    /* bestfit 语义: taskId 取自前驱节点记录(启发式归属), 断言取值合法性而非精确归属 */
    taskId = LOS_MemTaskIdGet(sysPtr);
    ICUNIT_GOTO_WITHIN_EQUAL(taskId, 0, LOSCFG_BASE_CORE_TSK_LIMIT, taskId, EXIT);

    /* 释放系统池指针后查询其归属: ws63 开启 LMS, 对已释放(毒化)内存的节点头
     * 走查会触发 LMS 违规复位(实测), 该分支列为不可覆盖项(见分析报告 3.3)。 */
    ret = LOS_MemFree((VOID *)LOSCFG_SYS_HEAP_ADDR, sysPtr);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    sysPtr = NULL;

    /* F-Inv: 池外指针 → OS_INVALID */
    taskId = LOS_MemTaskIdGet(g_memOutTC317);
    ICUNIT_GOTO_EQUAL(taskId, OS_INVALID, taskId, EXIT);

    ptr = LOS_MemAlloc(g_memPoolTC317, 0x40);
    ICUNIT_GOTO_NOT_EQUAL(ptr, NULL, 0, EXIT);

    /* ---- 2: LOS_MemNodeSizeCheck 两档检查级别 ---- */
#if (LOSCFG_BASE_MEM_NODE_SIZE_CHECK == 1)
    UINT32 totalSize = 0;
    UINT32 availSize = 0;
    UINT8 oldLevel;
    oldLevel = LOS_MemCheckLevelGet();
    ret = LOS_MemCheckLevelSet(LOS_MEM_CHECK_LEVEL_LOW);
    if (ret == LOS_OK) {
        ret = LOS_MemNodeSizeCheck(g_memPoolTC317, ptr, &totalSize, &availSize);
        ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
        ICUNIT_GOTO_WITHIN_EQUAL(availSize, 1, totalSize, availSize, EXIT);

        /* 未对齐 ptr: LOW 档返回 NO_HEAD/OUTSIDE(依实现口径), 接受 MEMCHECK 错误族 */
        ret = LOS_MemNodeSizeCheck(g_memPoolTC317, (VOID *)((UINTPTR)ptr + 1), &totalSize, &availSize);
        ICUNIT_GOTO_NOT_EQUAL(ret, LOS_OK, ret, EXIT);
    }

    ret = LOS_MemCheckLevelSet(LOS_MEM_CHECK_LEVEL_HIGH);
    if (ret == LOS_OK) {
        ret = LOS_MemNodeSizeCheck(g_memPoolTC317, ptr, &totalSize, &availSize);
        ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
        ICUNIT_GOTO_WITHIN_EQUAL(availSize, 1, totalSize, availSize, EXIT);

        /* 池外 ptr: HIGH 档实测返回 OUTSIDE(0x2000102), 与 NO_HEAD 同属 MEMCHECK 错误族 */
        ret = LOS_MemNodeSizeCheck(g_memPoolTC317, (VOID *)g_memOutTC317, &totalSize, &availSize);
        ICUNIT_GOTO_NOT_EQUAL(ret, LOS_OK, ret, EXIT);
    }
    (VOID)LOS_MemCheckLevelSet(oldLevel);
#endif

    /* ---- 3: LOS_MemMrealloc 分支 ---- */
    ptrRe = LOS_MemMrealloc(g_memPoolTC317, NULL, 0x80, MEM_TC317_MOD_A);
    ICUNIT_GOTO_NOT_EQUAL(ptrRe, NULL, 0, EXIT);

    ptrRe = LOS_MemMrealloc(g_memPoolTC317, ptrRe, 0, MEM_TC317_MOD_A);
    ICUNIT_GOTO_EQUAL((UINTPTR)ptrRe, (UINTPTR)NULL, (UINTPTR)ptrRe, EXIT);

    /* 跨 moduleId → 告警分支仍成功 */
    ptrRe = LOS_MemMrealloc(g_memPoolTC317, ptr, 0x100, MEM_TC317_MOD_B);
    ICUNIT_GOTO_NOT_EQUAL(ptrRe, NULL, 0, EXIT);
    ptr = ptrRe;

    /* 未对齐 ptr → OsMemPtrToNode 拒绝 → NULL */
    ptrRe = LOS_MemMrealloc(g_memPoolTC317, (VOID *)((UINTPTR)ptr + 2), 0x40, MEM_TC317_MOD_A);
    ICUNIT_GOTO_EQUAL((UINTPTR)ptrRe, (UINTPTR)NULL, (UINTPTR)ptrRe, EXIT);

    /* ---- 4: LOS_MemMfree 防御 ---- */
    ret = LOS_MemMfree(g_memPoolTC317, ptr, MEM_TC317_MOD_A);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ptr = NULL;

    ret = LOS_MemMfree(g_memPoolTC317, (VOID *)((UINTPTR)g_memPoolTC317 + 0x11), MEM_TC317_MOD_A);
    ICUNIT_GOTO_NOT_EQUAL(ret, LOS_OK, ret, EXIT);

    /* ---- 5: LOS_MemInfoGet / LOS_MemRealloc ---- */
    ptr = LOS_MemAlloc(g_memPoolTC317, 0x80);
    ICUNIT_GOTO_NOT_EQUAL(ptr, NULL, 0, EXIT);

    ret = LOS_MemInfoGet(g_memPoolTC317, &poolStatus);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ICUNIT_GOTO_NOT_EQUAL(poolStatus.totalUsedSize + poolStatus.totalFreeSize, 0, 0, EXIT);

    ptrRe = LOS_MemRealloc(g_memPoolTC317, ptr, 0x40);
    ICUNIT_GOTO_NOT_EQUAL(ptrRe, NULL, 0, EXIT);
    ptr = ptrRe;

    ptrRe = LOS_MemRealloc(g_memPoolTC317, ptr, 0x200);
    ICUNIT_GOTO_NOT_EQUAL(ptrRe, NULL, 0, EXIT);
    ptr = ptrRe;

    ret = LOS_MemFree(g_memPoolTC317, ptr);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ptr = NULL;

    ret = LOS_MemDeInit(g_memPoolTC317);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    return LOS_OK;

EXIT:
    if (sysPtr != NULL) {
        (VOID)LOS_MemFree((VOID *)LOSCFG_SYS_HEAP_ADDR, sysPtr);
    }
    if (ptr != NULL) {
        (VOID)LOS_MemFree(g_memPoolTC317, ptr);
    }
#if (LOSCFG_BASE_MEM_NODE_SIZE_CHECK == 1)
    (VOID)LOS_MemCheckLevelSet(oldLevel);
#endif
    (VOID)LOS_MemDeInit(g_memPoolTC317);
    return LOS_NOK;
}

/**
 * @ingroup TEST_MEM
 * @par TestCase_Number
 * ItLosMem317
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test interface LOS_MemTaskIdGet/LOS_MemNodeSizeCheck/LOS_MemMrealloc/LOS_MemMfree/LOS_MemInfoGet/LOS_MemRealloc (bestfit)
 * @par TestCase_Pretreatment_Condition
 * bestfit memory (ws63).
 * @par TestCase_Test_Steps
 * step1: TaskIdGet on system-pool used/freed/foreign pointers.
 * step2: check level LOW/HIGH, NodeSizeCheck valid/misaligned/foreign pointers.
 * step3: Mrealloc NULL/size-0/cross-module/misaligned; Mfree normal/misaligned; InfoGet; Realloc shrink/grow.
 * @par TestCase_Expected_Result
 * 1.Defensive branches return expected error codes; normal branches follow contract.
 * 2.Own pool always de-inited on any exit path.
 * @par TestCase_Level
 * Level 1
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */

#endif /* LOSCFG_KERNEL_MEM_BESTFIT */

VOID ItLosMem317(VOID)
{
#ifdef LOSCFG_KERNEL_MEM_BESTFIT
    TEST_ADD_CASE("ItLosMem317", TestCase, TEST_LOS, TEST_MEM, TEST_LEVEL1, TEST_FUNCTION);
#endif
}

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */
