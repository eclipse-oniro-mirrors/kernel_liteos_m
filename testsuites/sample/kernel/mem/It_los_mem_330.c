/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
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

#ifdef LOSCFG_KERNEL_MEM_BESTFIT

/* 用例简要描述: 空闲节点与END地址TaskIdGet均无效 */
static UINT32 TestCase(VOID)
{
    UINT32 taskId;
    VOID *memPtr = NULL;
    VOID *freePtr = NULL;
    VOID *endPtr = NULL;
    VOID *sysPool = (VOID *)LOSCFG_SYS_HEAP_ADDR;

    /* F-State: 系统堆分配(尺寸>SLAB 上限, 走 bestfit 节点)后释放,
     * LOS_MemTaskIdGet(已释放指针) 命中空闲节点分支(bestfit 1964-1967) */
    memPtr = LOS_MemAlloc(sysPool, 0x100);
    ICUNIT_GOTO_NOT_EQUAL(memPtr, NULL, 0, EXIT);
    freePtr = memPtr;

    ICUNIT_GOTO_EQUAL(LOS_MemFree(sysPool, memPtr), LOS_OK, LOS_OK, EXIT);
    memPtr = NULL;

    taskId = LOS_MemTaskIdGet(freePtr); /* 已释放: 归属空闲节点 -> OS_INVALID */
    ICUNIT_GOTO_EQUAL(taskId, OS_INVALID, taskId, EXIT);
    freePtr = NULL;

    /* F-State: END 节点地址(= 池首+池尺寸-节点头大小, 白盒偏移):
     * 过范围检查(<=END)但节点遍历未命中 -> OS_INVALID(bestfit 1972-1973) */
    endPtr = (VOID *)((UINTPTR)sysPool + LOS_MemPoolSizeGet(sysPool) - TEST_BF_NODE_HEAD_SIZE);
    taskId = LOS_MemTaskIdGet(endPtr);
    ICUNIT_GOTO_EQUAL(taskId, OS_INVALID, taskId, EXIT);

    return LOS_OK;

EXIT:
    if (memPtr != NULL) {
        (VOID)LOS_MemFree(sysPool, memPtr);
    }
    return LOS_OK;
}

#endif /* LOSCFG_KERNEL_MEM_BESTFIT */

VOID ItLosMem330(VOID)
{
#ifdef LOSCFG_KERNEL_MEM_BESTFIT
    TEST_ADD_CASE("ItLosMem330", TestCase, TEST_LOS, TEST_MEM, TEST_LEVEL1, TEST_FUNCTION);
#endif
}
