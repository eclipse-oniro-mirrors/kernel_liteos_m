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

#include "osTest.h"
#include "It_los_trace.h"

/*
 * @tc.name      ItLosTrace011
 * @tc.desc      MEM类事件掩码下重启trace触发内存信息事件
 * @tc.coverage branch: los_trace.c:237-239 OsTraceHook MEM_INFO_REQ (LOS_MemInfoGet + enqueue)
 */

/* 用例简要描述: MEM类事件掩码下重启trace触发内存信息事件
 * 1) 步骤0 显式 LOS_TraceEventMaskSet(TRACE_MEM_FLAG=0x100): 默认掩码
 *    TRACE_DEFAULT_MASK=0x60(los_trace_pri.h:55)不含 MEM 类, 且可消除前序用例
 *    残留的 USER 掩码, 保证步骤2 的事件命中过滤;
 * 2) 步骤1 LOS_TraceStop() 进入 STOPED(清 ring 缓冲);
 * 3) 步骤2 LOS_TraceStart(): STOPED->STARTED 迁移后固定发
 *    LOS_TRACE(MEM_INFO_REQ, m_aucSysMem0)(los_trace.c:421) -> OsTraceHook 中
 *    eventType & g_traceMask 命中 -> MEM_INFO_REQ 分支(los_trace.c:237-239,
 *    LOS_MemInfoGet 采池信息 + OsTraceEnqueue(MEM_INFO)), 断言返回 LOS_OK;
 * 4) 步骤3 再 Stop 后 LOS_TraceRecordDump(dump 仅 STOPED 态允许, 参照
 *    ItLosTrace006 惯例), 验证含 MEM_INFO 事件的缓冲可正常 dump 且无异常;
 * 5) 清理(无条件): 掩码恢复默认 TRACE_DEFAULT_MASK(0x60), LOS_TraceStart()
 *    恢复 STARTED 状态(幂等返回 LOS_OK), 不残留掩码/状态影响全量后续用例。
 * 板级: 仅 qemu(ws63 未编 LOSCFG_KERNEL_TRACE, 套件已由 osTest.h
 * LOS_KERNEL_TRACE_TEST 整体 gate, 本文件无需文件内 gate)。 */
static UINT32 Testcase(VOID)
{
    UINT32 ret;

    /* 步骤0: 显式置 MEM 类事件掩码(消除默认掩码过滤与前序 USER 掩码残留) */
    LOS_TraceEventMaskSet(TRACE_MEM_FLAG);

    /* 步骤1: 停止 trace, 进入 STOPED 并清 ring 缓冲 */
    LOS_TraceStop();

    /* 步骤2: 重启 trace: Start 内固定 LOS_TRACE(MEM_INFO_REQ, m_aucSysMem0)
     * 命中 los_trace.c:237-239 (MEM 类事件入队) */
    ret = LOS_TraceStart();
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* 步骤3: 回到 STOPED 后 dump(dump 仅 STOPED 允许), 缓冲含 MEM_INFO 事件,
     * 正常返回无异常即通过(dump 内容不做精确断言) */
    LOS_TraceStop();
    LOS_TraceRecordDump(FALSE);

EXIT:
    /* 清理(无条件): 恢复默认掩码 0x60 + 恢复 STARTED 状态(幂等) */
    LOS_TraceEventMaskSet(TRACE_DEFAULT_MASK);
    ret = LOS_TraceStart();
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    return LOS_OK;
}

VOID ItLosTrace011(VOID)
{
    TEST_ADD_CASE("ItLosTrace011", Testcase, TEST_LOS, TEST_TRACE, TEST_LEVEL1, TEST_FUNCTION);
}
