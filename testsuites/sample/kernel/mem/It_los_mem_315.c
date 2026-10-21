/*
 * Copyright (c) 2022-2022 Huawei Device Co., Ltd. All rights reserved.
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

#include "osTest.h"
#include "It_los_mem.h"

#if (LOS_KERNEL_TEST_MANUAL == 1) && defined(LOSCFG_KERNEL_MEM_BESTFIT)
/* 用例简要描述: LOS_MemIntegrityCheck（对外接口）堆损坏负路径（破坏性/不返回，
 * 清除已用节点 sizeAndFlag 的 USED 标志位（用户指针前最后一个字，各配置下
 * 布局稳定；本模块 It_los_mem.h 亦镜像内部节点结构，做法一致）→ 走查器将其
 * 视为 free 节点，pstPrev（对已用节点该位置为 magic=节点地址按位取反）必然
 * 落在合法池范围之外 → 进入错误打印链：
 * EXCINFO_PRINT_ERR → OsMemNodeInfo（EXCINFO_PRINTK + OsDumpMemByte/DumpByte，
 * 即 los_misc.c 全链）→ EXCINFO_PANIC → LOS_Panic（UartVprintf 输出）→
 * ArchTaskExit 关中断死循环；ws63 由看门狗约 10s 后复位板子。
 * 预期手工检查日志：memory check error 横幅 + broken node 十六进制转储
 * （OsDumpMemByte 输出）+ 任务信息 + backtrace 后复位。 */
static UINT32 Testcase(VOID)
{
    UINT32 ret;
    VOID *mem = NULL;

    MemStart();
    MemInit();

    mem = LOS_MemAlloc(g_memPool, 0x100); // 0x100, 普通分配尺寸
    ICUNIT_GOTO_NOT_EQUAL(mem, NULL, mem, EXIT);

    /* F-Inv: 清 USED 标志(高 bit31)，节点被走查器当作 free 节点，
     * pstPrev(=magic=节点地址取反)越界触发完整性检查错误链 */
    *((volatile UINT32 *)((UINTPTR)mem - sizeof(UINT32))) &= ~0x80000000U;

    /* 预期不返回：内部 EXCINFO_PANIC → LOS_Panic → ArchTaskExit；
     * 若后续内核行为调整为仅报错返回，此处仍校验返回值 */
    ret = LOS_MemIntegrityCheck(g_memPool);
    ICUNIT_GOTO_EQUAL(ret, LOS_NOK, ret, EXIT);

EXIT:
    MemFree();
    MemEnd();
    return LOS_OK;
}
#endif /* (LOS_KERNEL_TEST_MANUAL == 1) && defined(LOSCFG_KERNEL_MEM_BESTFIT) */

/**
 * @ingroup TEST_MEM
 * @par TestCase_Number
 * ItLosMem315
 * @par TestCase_TestCase_Type
 * Destructive function test (MANUAL only)
 * @brief Test interface LOS_MemIntegrityCheck heap-corruption dump chain
 * @par TestCase_Pretreatment_Condition
 * LOS_KERNEL_TEST_MANUAL=1 + bestfit memory (ws63).
 * @par TestCase_Test_Steps
 * step1: MemInit a dedicated pool, LOS_MemAlloc one block
 * step2: clear the USED flag in the node sizeAndFlag word (ptr - 4)
 * step3: call LOS_MemIntegrityCheck(pool)
 * @par TestCase_Expected_Result
 * 1. EXCINFO_PRINT_ERR banner "memory check error" printed
 * 2. broken-node hex dump via OsDumpMemByte (los_misc.c) printed
 * 3. task info + backtrace printed, then LOS_Panic → ArchTaskExit,
 *    board resets by watchdog (~10s) — suite does NOT continue
 * @par TestCase_Level
 * Level 1
 * @par TestCase_Automated
 * false
 * @par TestCase_Remark
 * DESTRUCTIVE/NON-RETURNING: covers los_misc.c DumpByte/OsDumpMemByte and
 * los_exc.c WriteExcInfoToBuf/WriteExcBufVa guard + los_printf.c UartVprintf
 * (via EXCINFO_PANIC → LOS_Panic). Only for manual runs (watchdog reboot).
 */
VOID ItLosMem315(void)
{
#if (LOS_KERNEL_TEST_MANUAL == 1) && defined(LOSCFG_KERNEL_MEM_BESTFIT)
    TEST_ADD_CASE("ItLosMem315", Testcase, TEST_LOS, TEST_MEM, TEST_LEVEL1, TEST_FUNCTION);
#endif
}
