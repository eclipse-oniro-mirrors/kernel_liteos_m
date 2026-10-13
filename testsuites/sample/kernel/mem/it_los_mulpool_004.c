/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2023 Huawei Device Co., Ltd. All rights reserved.
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
 * 3. Neither the name of the copyright holder nor that of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "It_los_mem.h"

#ifdef LOSCFG_MEM_MUL_POOL_ALLOC

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

/* 用例简要描述: 跨池realloc数据一致且旧块释放 */
static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT32 i;
    UINT32 inPool0;
    VOID *ptr = NULL;
    VOID *filler = NULL;
    VOID *newPtr = NULL;
    UINT32 ptrSize = 0x100;   /* > SLAB 上限(128), 保证走 bestfit 节点 */
    UINT32 fillerSize = 0x800;
    UINT32 reallocSize = 0x1000;

    g_poolNum = 0;
    ret = LOS_MulPoolRegister(TestGetAddrSize, TestReleaseAddrSize);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* 池0: 小块(填充数据) + 大块吃掉池0余量 */
    ptr = LOS_MulPoolAlloc(ptrSize);
    ICUNIT_GOTO_NOT_EQUAL(ptr, NULL, 0, EXIT);
    for (i = 0; i < ptrSize; i++) {
        *((UINT8 *)ptr + i) = (UINT8)(0x5A ^ i);
    }

    filler = LOS_MulPoolAlloc(fillerSize);
    ICUNIT_GOTO_NOT_EQUAL(filler, NULL, 0, EXIT);

    /* F-Pos: 池0 余量不足以原地扩容 -> LOS_MemRealloc 失败 ->
     * LOS_MulPoolAlloc 扩展新池 -> OsMulPoolCopyToNewNode 跨池拷贝 +
     * 旧块自动释放(multipool 296-303, bestfit 2449-2468) */
    newPtr = LOS_MulPoolRealloc(ptr, reallocSize);
    ICUNIT_GOTO_NOT_EQUAL(newPtr, NULL, 0, EXIT);
    ptr = NULL; /* 跨池 realloc 成功路径内部已释放旧块 */

    /* 新指针落在扩容池(池0 地址范围之外) */
    inPool0 = (UINT32)(((UINTPTR)newPtr >= (UINTPTR)g_poolMem[0]) &&
        ((UINTPTR)newPtr < ((UINTPTR)g_poolMem[0] + TEST_EACH_POOL_SIZE)));
    ICUNIT_GOTO_EQUAL(inPool0, 0, inPool0, EXIT);

    /* 拷贝数据与旧块一致(前 ptrSize 字节) */
    for (i = 0; i < ptrSize; i++) {
        ICUNIT_GOTO_EQUAL(*((UINT8 *)newPtr + i), (UINT8)(0x5A ^ i), i, EXIT);
    }

    /* 扩容池数据可写(完整 reallocSize 范围)后释放 */
    (VOID)memset_s(newPtr, reallocSize, 0xC3, reallocSize);
    ret = LOS_MulPoolFree(newPtr);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    newPtr = NULL;

    ret = LOS_MulPoolFree(filler);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    filler = NULL;

    ret = LOS_MulPoolShrink();
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    TEST_MulPoolUnRegister();
    return LOS_OK;

EXIT:
    if (ptr != NULL) {
        (VOID)LOS_MulPoolFree(ptr);
    }
    if (newPtr != NULL) {
        (VOID)LOS_MulPoolFree(newPtr);
    }
    if (filler != NULL) {
        (VOID)LOS_MulPoolFree(filler);
    }
    (VOID)LOS_MulPoolShrink();
    TEST_MulPoolUnRegister();
    return LOS_OK;
}

/**
 * @ingroup TEST_MISC
 * @par TestCase_Number
 * ItLosMulPool004
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test interface LOS_MulPoolRealloc/LOS_MulPoolFree (cross-pool copy path)
 * @par TestCase_Pretreatment_Condition
 * LOSCFG_MEM_MUL_POOL_ALLOC enabled.
 * @par TestCase_Test_Steps
 * step1: register mulpool hooks (own pool chain, g_poolNum reset).
 * step2: pool0 small block with pattern + filler block exhausting pool0 room.
 * step3: LOS_MulPoolRealloc(small, 0x1000): in-place fails, expand pool,
 *        cross-pool copy, old block auto-freed.
 * step4: verify new pointer lands outside pool0, data preserved; free all,
 *        shrink, unregister.
 * @par TestCase_Expected_Result
 * 1.Realloc returns non-NULL pointer outside pool0's range with data preserved.
 * 2.Old block freed inside the realloc path; all frees/shrink return LOS_OK.
 * @par TestCase_Level
 * Level 1
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */

#endif /* LOSCFG_MEM_MUL_POOL_ALLOC */

VOID ItLosMulPool004(VOID)
{
#ifdef LOSCFG_MEM_MUL_POOL_ALLOC
    TEST_ADD_CASE("ItLosMulPool004", TestCase, TEST_LOS, TEST_MEM, TEST_LEVEL1, TEST_FUNCTION);
#endif
}

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */
