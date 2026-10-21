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
#include "shcmdparse.h"
#include "los_memory.h"

#if defined(LOSCFG_SHELL)

/* 用例简要描述: shell 框架辅助对外接口直调: OsCmdKeyShift(NULL/长度超限/前导空格剥离)、
 * OsCmdParse(NULL/空串参数防御)、OsShellKeyInit(NULL 防御)、
 * OsShellKeyDeInit(NULL/空链表/带节点链表逐节点释放) */
static UINT32 Testcase(VOID)
{
    UINT32 ret;
    CHAR buf[CMD_KEY_LEN];
    CmdParsed cmdParsed;
    CmdKeyLink *keyLink = NULL;
    CmdKeyLink *node1 = NULL;
    CmdKeyLink *node2 = NULL;

    /* F-Inv: OsCmdKeyShift cmdKey 为 NULL */
    ret = OsCmdKeyShift(NULL, buf, sizeof(buf));
    ICUNIT_GOTO_EQUAL(ret, (UINT32)OS_ERROR, ret, EXIT);

    /* F-Inv: OsCmdKeyShift cmdOut 为 NULL */
    ret = OsCmdKeyShift("abc", NULL, sizeof(buf));
    ICUNIT_GOTO_EQUAL(ret, (UINT32)OS_ERROR, ret, EXIT);

    /* F-Inv: OsCmdKeyShift 输入长度 >= 输出缓冲(截断拒绝) */
    ret = OsCmdKeyShift("abcdef", buf, 4); /* 4: 小于 6 字符输入长度 */
    ICUNIT_GOTO_EQUAL(ret, (UINT32)OS_ERROR, ret, EXIT);

    /* F-Pos: OsCmdKeyShift 前导空格剥离与压缩 */
    ret = OsCmdKeyShift("  ab  c ", buf, sizeof(buf));
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ICUNIT_GOTO_STRING_EQUAL(buf, "ab c", 0, EXIT);

    /* F-Inv: OsCmdParse 参数防御(cmdStr/cmdParsed 为 NULL 与空串) */
    ret = OsCmdParse(NULL, NULL);
    ICUNIT_GOTO_EQUAL(ret, (UINT32)OS_ERROR, ret, EXIT);
    ret = OsCmdParse((CHAR *)"", &cmdParsed);
    ICUNIT_GOTO_EQUAL(ret, (UINT32)OS_ERROR, ret, EXIT);

    /* F-Inv: OsShellKeyInit shellCB 为 NULL */
    ret = OsShellKeyInit(NULL);
    ICUNIT_GOTO_EQUAL(ret, (UINT32)OS_ERROR, ret, EXIT);

    /* F-Pos: OsShellKeyDeInit NULL 直接返回 */
    OsShellKeyDeInit(NULL);

    /* F-Pos: OsShellKeyDeInit 空链表(仅头节点自身)与带节点链表逐节点释放 */
    keyLink = (CmdKeyLink *)LOS_MemAlloc(m_aucSysMem0, sizeof(CmdKeyLink));
    ICUNIT_GOTO_NOT_EQUAL(keyLink, NULL, 0, EXIT);
    (VOID)memset_s(keyLink, sizeof(CmdKeyLink), 0, sizeof(CmdKeyLink));
    keyLink->count = 0;
    LOS_ListInit(&(keyLink->list));
    OsShellKeyDeInit(keyLink); /* 空链表: 循环不进入,直接释放 */
    keyLink = NULL;

    keyLink = (CmdKeyLink *)LOS_MemAlloc(m_aucSysMem0, sizeof(CmdKeyLink));
    ICUNIT_GOTO_NOT_EQUAL(keyLink, NULL, 0, EXIT);
    (VOID)memset_s(keyLink, sizeof(CmdKeyLink), 0, sizeof(CmdKeyLink));
    keyLink->count = 0;
    LOS_ListInit(&(keyLink->list));
    node1 = (CmdKeyLink *)LOS_MemAlloc(m_aucSysMem0, sizeof(CmdKeyLink));
    node2 = (CmdKeyLink *)LOS_MemAlloc(m_aucSysMem0, sizeof(CmdKeyLink));
    if ((node1 == NULL) || (node2 == NULL)) {
        goto EXIT;
    }
    (VOID)memset_s(node1, sizeof(CmdKeyLink), 0, sizeof(CmdKeyLink));
    (VOID)memset_s(node2, sizeof(CmdKeyLink), 0, sizeof(CmdKeyLink));
    LOS_ListTailInsert(&(keyLink->list), &(node1->list));
    LOS_ListTailInsert(&(keyLink->list), &(node2->list));
    keyLink->count = 2; /* 2: 两个历史节点 */
    OsShellKeyDeInit(keyLink); /* 逐节点释放后释放链表头 */
    keyLink = NULL;
    node1 = NULL;
    node2 = NULL;

    return LOS_OK;
EXIT:
    /* 异常路径同样释放构造的链表(尽力而为) */
    if (node2 != NULL) {
        (VOID)LOS_MemFree(m_aucSysMem0, node2);
    }
    if (node1 != NULL) {
        (VOID)LOS_MemFree(m_aucSysMem0, node1);
    }
    if (keyLink != NULL) {
        (VOID)LOS_MemFree(m_aucSysMem0, keyLink);
    }
    return LOS_OK;
}

/*
 * @tc.name     ItLosShell020
 * @tc.desc     验证 shell 框架辅助对外接口: OsCmdKeyShift 的 NULL/长度超限/前导
 *              空格剥离, OsCmdParse 参数防御, OsShellKeyInit NULL 防御,
 *              OsShellKeyDeInit 的 NULL/空链表/带节点链表逐节点释放
 */
VOID ItLosShell020(VOID)
{
    TEST_ADD_CASE("ItLosShell020", Testcase, TEST_LOS, TEST_SHELL_MOD, TEST_LEVEL1, TEST_FUNCTION);
}

#endif /* LOSCFG_SHELL */
