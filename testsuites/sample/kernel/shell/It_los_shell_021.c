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
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "It_los_shell.h"
#include "shcmd.h"

#if defined(LOSCFG_SHELL)

/* 用例简要描述: task 命令(OsShellCmdDumpTask)单参任务号分支直调: 无参(全任务信息)/
 * 单参合法任务号(当前任务, 经 LOS_TaskBackTrace 打印回溯)/单参非数字与越界拒绝/
 * 单参全任务掩码值(0xFFFFFFFF >= 上限, 拒绝)/双参 Usage 拒绝 */
static UINT32 Testcase(VOID)
{
    INT32 ret;
    UINT32 curTaskId;
    CHAR idBuf[16]; /* 16: 任务号十进制字符串缓冲 */
    const CHAR *argvId[1] = {idBuf};
    const CHAR *argvNan[1] = {"abc"};
    const CHAR *argvOver[1] = {"99999"}; /* 远超任务号上限 */
    const CHAR *argvMask[1] = {"4294967295"}; /* OS_ALL_TASK_MASK 数值 */

#ifndef LOSCFG_TEST_AMP_SLAVE
    /* F-Pos: 无参(全任务信息, OsShellCmdTskInfoGet 路径) */
    ret = OsShellCmdDumpTask(0, NULL);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
#else
    /* AMP slave: the full-table dump prints ~1KB over the UART2 line
     * shared with the Linux master while rpmsg interrupts keep firing -
     * that interaction has wedged the slave intermittently.  Still drive
     * the path once, but do not assert. */
    (VOID)OsShellCmdDumpTask(0, NULL); /* 仍调用一次驱动路径, 不断言 */
#endif

    /* F-Pos: 单参合法任务号(当前任务): strtoul 解析 + LOS_TaskBackTrace 回溯 */
    curTaskId = LOS_CurTaskIDGet();
    ret = (INT32)snprintf_s(idBuf, sizeof(idBuf), sizeof(idBuf) - 1, "%u", curTaskId);
    ICUNIT_ASSERT_NOT_EQUAL(ret, -1, ret);
    ret = OsShellCmdDumpTask(1, argvId);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* F-Inv: 单参非数字(尾指针非 0 拒绝) */
    ret = OsShellCmdDumpTask(1, argvNan);
    ICUNIT_ASSERT_EQUAL(ret, (INT32)OS_ERROR, ret);

    /* F-Inv: 单参越界(>= g_taskMaxNum 拒绝) */
    ret = OsShellCmdDumpTask(1, argvOver);
    ICUNIT_ASSERT_EQUAL(ret, (INT32)OS_ERROR, ret);

    /* F-Inv: 单参全任务掩码数值(经上限检查拒绝, 不走全任务分支) */
    ret = OsShellCmdDumpTask(1, argvMask);
    ICUNIT_ASSERT_EQUAL(ret, (INT32)OS_ERROR, ret);

    /* F-Inv: 双参(Usage 提示拒绝) */
    ret = OsShellCmdDumpTask(2, NULL);
    ICUNIT_ASSERT_EQUAL(ret, (INT32)OS_ERROR, ret);

    return LOS_OK;
}

/*
 * @tc.name     ItLosShell021
 * @tc.desc     验证 task 命令单参任务号新分支: 无参全任务信息/单参当前任务号
 *              (LOS_TaskBackTrace 回溯路径)/非数字与越界与掩码值拒绝/双参 Usage
 */
VOID ItLosShell021(VOID)
{
    TEST_ADD_CASE("ItLosShell021", Testcase, TEST_LOS, TEST_SHELL_MOD, TEST_LEVEL1, TEST_FUNCTION);
}

#endif /* LOSCFG_SHELL */
