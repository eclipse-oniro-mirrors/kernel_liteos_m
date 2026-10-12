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
 *    of the conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
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

#include "It_los_shell.h"

VOID ItSuiteLosShell(VOID)
{
#ifdef LOSCFG_SHELL_DMESG
    ItLosShell001();
    ItLosShell008(); /* dmesg 边界深化: 环绕读回一致性/LvSet-LvGet 往返/重装清零 */
#endif
#ifdef LOSCFG_SHELL_LK
    ItLosShell002();
    ItLosShell004();
    ItLosShell005();
    ItLosShell006(); /* osCmdReg 运行时注册+OsCmdExec 触发回调+help 直调 */
    ItLosShell007(); /* ExecCmdline 解析分发链: 带参/空行容错/command not found */
    ItLosShell009();
#endif
#if defined(LOSCFG_SHELL_LK) && defined(LOSCFG_SHELL_DMESG)
    ItLosShell003();
#endif
#ifdef LOSCFG_SHELL
    ItLosShell010(); /* VfsNormalizePath 直调全分支(纯字符串运算,双板) */
    ItLosShell016(); /* free/memusage 全参数分支 */
#endif
#if defined(LOSCFG_SHELL) && defined(LOSCFG_FS_VFS)
    ItLosShell011(); /* vfs 命令参数错误/规范化失败路径(双板确定性) */
#endif
#if defined(LOSCFG_SHELL) && defined(LOSCFG_FS_LITTLEFS)
    ItLosShell012(); /* ls/cd/pwd 成功路径(自包含,无挂载副作用) */
    ItLosShell013(); /* touch/cat/mkdir 成功与失败路径 */
    ItLosShell014(); /* cp 全分支(含通配符/深路径/fd 耗尽) */
    ItLosShell015(); /* rm/rmdir 全分支(含通配符矩阵/句柄耗尽/极限路径) */
#endif
#if defined(LOSCFG_SHELL) && defined(LOSCFG_SHELL_LK)
    ItLosShell017(); /* log 命令(CmdLog)全分支+根挂载 ls+LK/dmesg 联动(自包含) */
    ItLosShell018(); /* ExecCmdline 引号路径(命令名/含空格参数/单引号过滤) */
#endif
#if defined(LOSCFG_SHELL) && defined(LOSCFG_SHELL_DMESG)
    ItLosShell019(); /* dmesg 命令全选项 + 环形缓冲块写路径 */
#endif
#ifdef LOSCFG_SHELL
    ItLosShell020(); /* 框架辅助接口(OsCmdKeyShift/OsCmdParse/OsShellKey*) */
    ItLosShell021(); /* task 命令单参任务号分支(合法回溯/非数字/越界/掩码/双参) */
#endif
}
