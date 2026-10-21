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

#ifdef LOSCFG_KERNEL_MEM_TLSF

/* 用例简要描述: 链中删中节点合并及小桶空闲打印 */
static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT32 size = 0x20; /* 5 same-size blocks, node size 0x30 -> small bucket index */
    VOID *p[5] = {NULL}; /* p1..p5 */
    VOID *a = NULL;
    VOID *b = NULL;
    VOID *c = NULL;
    INT32 i;

    MemStart();

    MemInit();

    /* F-State: 同尺寸 5 连续分配 p1..p5(相邻) */
    for (i = 0; i < 5; i++) {
        p[i] = LOS_MemAlloc(g_memPool, size);
        ICUNIT_GOTO_NOT_EQUAL(p[i], NULL, 0, EXIT);
    }

    /* free p3 -> 链[p3]; free p1 -> 链[p1,p3]; free p5 -> 链[p5,p1,p3]
     * 每次释放成功立即置空: 后续任一断言失败走 EXIT 清理时防重复释放 */
    ret = LOS_MemFree(g_memPool, p[2]); /* p3 */
    if (ret == LOS_OK) {
        p[2] = NULL;
    }
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = LOS_MemFree(g_memPool, p[0]); /* p1 */
    if (ret == LOS_OK) {
        p[0] = NULL;
    }
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = LOS_MemFree(g_memPool, p[4]); /* p5 */
    if (ret == LOS_OK) {
        p[4] = NULL;
    }
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* free p2: 合并前块 p1 时从链表中部删除 p1(非链表头且带后继 p3),
     * 覆盖 tlsf OsMemListDelete 的 node->next->prev = node->prev 分支(755) */
    ret = LOS_MemFree(g_memPool, p[1]); /* p2 */
    if (ret == LOS_OK) {
        p[1] = NULL;
    }
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    ret = LOS_MemFree(g_memPool, p[3]); /* p4, 回收剩余占用 */
    if (ret == LOS_OK) {
        p[3] = NULL;
    }
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* F-Pos: 孤立小空闲块后 FreeNodeShow, 覆盖小桶(index<OS_MEM_SMALL_BUCKET_COUNT)
     * 空闲节点尺寸打印分支(tlsf 1932): A/B/C 各 0x20, free B 被 A/C 隔离成 0x30 小空闲块 */
    a = LOS_MemAlloc(g_memPool, size);
    ICUNIT_GOTO_NOT_EQUAL(a, NULL, 0, EXIT);
    b = LOS_MemAlloc(g_memPool, size);
    ICUNIT_GOTO_NOT_EQUAL(b, NULL, 0, EXIT);
    c = LOS_MemAlloc(g_memPool, size);
    ICUNIT_GOTO_NOT_EQUAL(c, NULL, 0, EXIT);

    ret = LOS_MemFree(g_memPool, b);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    b = NULL;

    ret = LOS_MemFreeNodeShow(g_memPool);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    ret = LOS_MemFree(g_memPool, a);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    a = NULL;
    ret = LOS_MemFree(g_memPool, c);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    c = NULL;

    /* 收尾: 池完整性校验 */
    ret = LOS_MemIntegrityCheck(g_memPool);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

EXIT:
    for (i = 0; i < 5; i++) {
        if (p[i] != NULL) {
            (VOID)LOS_MemFree(g_memPool, p[i]);
            p[i] = NULL;
        }
    }
    if (a != NULL) {
        (VOID)LOS_MemFree(g_memPool, a);
    }
    if (b != NULL) {
        (VOID)LOS_MemFree(g_memPool, b);
    }
    if (c != NULL) {
        (VOID)LOS_MemFree(g_memPool, c);
    }
    MemFree();
    MemEnd();
    return LOS_OK;
}

#endif /* LOSCFG_KERNEL_MEM_TLSF */

VOID ItLosMem319(VOID)
{
#ifdef LOSCFG_KERNEL_MEM_TLSF
    TEST_ADD_CASE("ItLosMem319", TestCase, TEST_LOS, TEST_MEM, TEST_LEVEL1, TEST_FUNCTION);
#endif
}
