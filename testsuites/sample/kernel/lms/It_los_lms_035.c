/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 * conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
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
#include "It_los_lms.h"

/* 用例简要描述: 仅用对外接口(LOS_MemAlloc/Free 释放序列)覆盖 LMS 释放标记
 * 钩子的节点合并路径 —— 多种释放顺序构造相邻空闲块的前向/后向合并, 被合并
 * 吸收的节点头被分配器清零, 后续释放的释放标记钩子按"后继节点首字节是否
 * 为 0"决定是否将后继节点头一并标记 AFTERFREE; 大块分配-释放-再切分再
 * 全释放, 构造更多合并拓扑; 全程堆保持一致, 最后反初始化恢复现场。 */

#define LMS035_POOL_SIZE 2048
#define LMS035_ALLOC_SIZE 32
#define LMS035_BIG_SIZE 256
#define LMS035_ALLOC_CNT 6

static UINT64 g_lms035Pool[LMS035_POOL_SIZE / sizeof(UINT64)];

static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT32 i;
    UINT8 *mem[LMS035_ALLOC_CNT] = {NULL};
    UINT8 *big = NULL;
    UINT8 *part1 = NULL;
    UINT8 *part2 = NULL;

    (VOID)LOS_MemDeInit(g_lms035Pool);
    ret = LOS_MemInit(g_lms035Pool, LMS035_POOL_SIZE);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    for (i = 0; i < LMS035_ALLOC_CNT; i++) {
        mem[i] = (UINT8 *)LOS_MemAlloc(g_lms035Pool, LMS035_ALLOC_SIZE);
        ICUNIT_GOTO_NOT_EQUAL((UINTPTR)mem[i], (UINTPTR)NULL, (UINTPTR)mem[i], EXIT);
    }

    /* 顺序 1: 先释放 C 再释放 B —— B 的后继为已释放节点 */
    ret = LOS_MemFree(g_lms035Pool, mem[2]); /* mem[2]: C */
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = LOS_MemFree(g_lms035Pool, mem[1]); /* mem[1]: B */
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* 顺序 2: 先释放 B 再释放 A —— A 与空闲 B 合并 */
    ret = LOS_MemFree(g_lms035Pool, mem[4]); /* mem[4]: E */
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = LOS_MemFree(g_lms035Pool, mem[3]); /* mem[3]: D 与 E 相邻合并 */
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* 顺序 3: 释放 A 触发与后方空闲区合并 */
    ret = LOS_MemFree(g_lms035Pool, mem[0]); /* mem[0]: A */
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* 顺序 4: 释放 F 触发与前向空闲区合并 */
    ret = LOS_MemFree(g_lms035Pool, mem[5]); /* mem[5]: F */
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* 大块分配-释放-再切分: 构造"清零节点区域被重新切分"的合并拓扑 */
    big = (UINT8 *)LOS_MemAlloc(g_lms035Pool, LMS035_BIG_SIZE);
    ICUNIT_GOTO_NOT_EQUAL((UINTPTR)big, (UINTPTR)NULL, (UINTPTR)big, EXIT);
    ret = LOS_MemFree(g_lms035Pool, big);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    part1 = (UINT8 *)LOS_MemAlloc(g_lms035Pool, LMS035_BIG_SIZE / 2);
    ICUNIT_GOTO_NOT_EQUAL((UINTPTR)part1, (UINTPTR)NULL, (UINTPTR)part1, EXIT);
    part2 = (UINT8 *)LOS_MemAlloc(g_lms035Pool, LMS035_BIG_SIZE / 2);
    ICUNIT_GOTO_NOT_EQUAL((UINTPTR)part2, (UINTPTR)NULL, (UINTPTR)part2, EXIT);
    ret = LOS_MemFree(g_lms035Pool, part2);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = LOS_MemFree(g_lms035Pool, part1);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

EXIT:
    (VOID)LOS_MemDeInit(g_lms035Pool);
    return LOS_OK;
}

VOID ItLosLms035(void)
{
    TEST_ADD_CASE("ItLosLms035", TestCase, TEST_LOS, TEST_LMS, TEST_LEVEL1, TEST_FUNCTION);
}
