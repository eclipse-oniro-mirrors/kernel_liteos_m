/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 * of conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific prior written
 * permission.
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

/* 用例简要描述: 【BUG_25 缺陷回归看护用例(默认隔离)】LOS_MemFree 对非法节点地址
 * (块内偏移 ±16)应返回错误码而不崩核
 * 【现象】    RK3568 从核(aarch64/bestfit)特定镜像布局下, p0+16 伪节点的 pstNext
 *             指向未映射地址(0x9ec69544), LOS_MemFree 解引用写链触发 L1
 *             Translation Fault(WRITE) 崩核, ExcSlavePoweroff 关断全套件
 *             (同镜像两轮确定性复现, ItLosMem012 通过后即崩)
 * 【根因】    kernel/liteos_m/kernel/base/mem/bestfit/los_memory.c:OsMemFreeNode/
 *             OsMemListDelete: free 合并路径对 prev/next 空闲节点指针与 magic
 *             未做"先校验后写链", 非法节点直接进入链表写路径
 * 【修复方向】合并前校验节点指针合法性与 magic, 坏链节点返回 LOS_NOK 不致崩核
 * 【恢复方法】修复后把 bug_isolate.h 中 KERNEL_BUG_25_MEMFREE_WILD_DEREF 置 1,
 *             用例恢复注册执行(聚合注册点隔离, it_los_mem.c)
 * 【关联】    BUG_25 / 本地问题记录 20261107_0115(与 20261104_162007 ItLosMux007
 *             插桩崩核同根因)
 */

#include "osTest.h"
#include "It_los_mem.h"


static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT32 size;

    void *p0 = NULL;

    MemInit();

    size = 0x1000;
    p0 = LOS_MemAlloc(g_memPool, size);
    ICUNIT_GOTO_NOT_EQUAL(p0, NULL, 0, EXIT);

    ret = LOS_MemFree(g_memPool, (void *)((char *)p0 + 16)); // 16, Get an illegal memory node address.
    ICUNIT_GOTO_EQUAL(ret, LOS_NOK, ret, EXIT);

    ret = LOS_MemFree(g_memPool, p0);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    ret = LOS_MemFree(g_memPool, (void *)((char *)p0 + 16)); // 16, Get an illegal memory node address.
    ICUNIT_GOTO_EQUAL(ret, LOS_NOK, ret, EXIT);

    p0 = LOS_MemAlloc(g_memPool, size);
    ICUNIT_GOTO_NOT_EQUAL(p0, NULL, 0, EXIT);

    ret = LOS_MemFree(g_memPool, (void *)((char *)p0 - 16)); // 16, Get an illegal memory node address.
    ICUNIT_GOTO_EQUAL(ret, LOS_NOK, ret, EXIT);

    ret = LOS_MemFree(g_memPool, p0);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    ret = LOS_MemFree(g_memPool, (void *)((char *)p0 - 16)); // 16, Get an illegal memory node address.
    ICUNIT_GOTO_EQUAL(ret, LOS_NOK, ret, EXIT);

EXIT:
    MemFree();
    return LOS_OK;
}

VOID ItLosMem013(void)
{
    TEST_ADD_CASE("ItLosMem013", TestCase, TEST_LOS, TEST_FN_KERNEL_DYNMEM, TEST_LEVEL1, TEST_FUNCTION);
}

