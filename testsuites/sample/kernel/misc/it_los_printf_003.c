/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 * conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor that of its contributors may be used
 * to endorse or promote products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "it_los_misc.h"
#include "los_printf.h"

#if (LOS_KERNEL_TEST_MANUAL == 1)
/* 用例简要描述: LOS_Panic（对外接口）致命错误输出路径（破坏性/不返回，仅供
 * MANUAL 模式手工执行，参考 It_los_exc_004~009 惯例）：
 * LOS_Panic 经 UartVprintf 直接输出 panic 信息（los_printf.c 中 UartVprintf
 * 的唯一调用链），随后 OsDoExcHook(EXC_PANIC) → LOS_BackTrace 打印回溯 →
 * ArchTaskExit 关中断死循环：ws63 由看门狗约 10s 后复位；qemu 无看门狗，
 * 需手工终止模拟器进程。 */
static UINT32 TestCase(VOID)
{
    /* F-Pos: 验证 panic 文本经 UartVprintf 原样输出 + 回溯打印后系统停机 */
    LOS_Panic("[ItLosPrintf003] manual LOS_Panic: expect UartVprintf output + backtrace\n");

    return LOS_OK; /* 不可达：LOS_Panic 为 NORETURN */
}
#endif /* LOS_KERNEL_TEST_MANUAL == 1 */

/**
 * @ingroup TEST_MISC
 * @par TestCase_Number
 * ItLosPrintf003
 * @par TestCase_TestCase_Type
 * Destructive function test (MANUAL only)
 * @brief Test interface LOS_Panic output path (UartVprintf)
 * @par TestCase_Pretreatment_Condition
 * LOS_KERNEL_TEST_MANUAL=1.
 * @par TestCase_Test_Steps
 * step1: call LOS_Panic with a marker message
 * @par TestCase_Expected_Result
 * 1. the marker message printed via UartVprintf
 * 2. backtrace printed, then ArchTaskExit halts (ws63: watchdog reset ~10s;
 *    qemu: no watchdog, kill the emulator manually)
 * @par TestCase_Level
 * Level 1
 * @par TestCase_Automated
 * false
 * @par TestCase_Remark
 * DESTRUCTIVE/NON-RETURNING: covers los_printf.c UartVprintf (its only caller
 * is LOS_Panic). Not runnable in the automated suite by design.
 */
VOID ItLosPrintf003(VOID)
{
#if (LOS_KERNEL_TEST_MANUAL == 1)
    TEST_ADD_CASE("ItLosPrintf003", TestCase, TEST_LOS, TEST_MISC, TEST_LEVEL1, TEST_FUNCTION);
#endif
}
