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
 *    of the following disclaimer in the documentation and/or materials provided with the
 *    distribution.
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

#include "it_los_misc.h"

VOID ItSuiteLosMisc(VOID)
{
    ItLosIo001();
    ItLosIo002();
    ItLosIo003();
    ItLosBase001();
    ItLosBase002();
    ItLosCompiler001();
    ItLosErr001();
    ItLosPrintf001();
    ItLosPrintf002();
    ItLosHook001(); /* LOS_HookReg/UnReg 全类型自适应矩阵(仅 qemu: ws63 未编译 hook) */
    ItLosBacktrace001(); /* LOS_RecordLR/LOS_BackTrace 栈来源分支 */
#if (LOS_KERNEL_TEST_MANUAL == 1)
    /* DESTRUCTIVE/NON-RETURNING: LOS_Panic output path, manual only */
    ItLosPrintf003();
#endif
#if defined(LOSCFG_ARCH_ARM)
    ItLosInit001();
#endif
#if defined(LOSCFG_ARCH_RISCV)
    ItLosSysClock001(); /* ws63 board.c LOS_SetSysClosk/LOS_GetSysClosk 回读 */
#endif
#if defined(LOSCFG_PLATFORM_QEMU_ARM_VIRT_CM4)
    ItLosPrintf004(); /* qemu 板级 dprintf.c printf 错误分支 */
#endif
}
