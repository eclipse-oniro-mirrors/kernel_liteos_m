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
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor that of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "it_los_misc.h"

#if defined(LOSCFG_PLATFORM_QEMU_ARM_VIRT_CM4)
/* 用例简要描述: printf传NULL走错误分支返回负值
 * F-Inv: printf(NULL) -> vsnprintf_s 对 NULL fmt 返回 -1(securec
 * vsnprintf_s.c 错误路径) -> dprintf.c:61 "printf error!" 分支(覆盖目标,
 * 经 dputs 串口原语输出) -> printf 透传返回负值; 断言返回值 <= 0 且用例
 * 正常返回(系统存活, 负路径唯一副作用为一行串口错误输出, 无资源申请无需
 * GOTO 清理)。板级 gate: qemu_mini_system_demo 产品配置宏(printf 经 map
 * 实测链接到 device/qemu/arm_mps2_an386/liteos_m/board/libc/dprintf.c);
 * ws63 上 printf 链接到 musl, 为避免板外意外行为编译期排除。 */
static UINT32 TestCase(VOID)
{
    INT32 ret;

    /* F-Inv: fmt=NULL, vsnprintf_s 失败 -> dprintf.c:61 "printf error!" -> 返回负值 */
    ret = printf(NULL);
    ICUNIT_ASSERT_WITHIN_EQUAL(ret, -1, 0, ret);

    return LOS_OK;
}
#endif /* LOSCFG_PLATFORM_QEMU_ARM_VIRT_CM4 */

VOID ItLosPrintf004(VOID)
{
#if defined(LOSCFG_PLATFORM_QEMU_ARM_VIRT_CM4)
    TEST_ADD_CASE("ItLosPrintf004", TestCase, TEST_LOS, TEST_MISC, TEST_LEVEL1, TEST_FUNCTION);
#endif
}
