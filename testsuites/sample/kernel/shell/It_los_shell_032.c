/*
 * Copyright (c) 2026-2026 Huawei Device Co., Ltd. All rights reserved.
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
 * 3. Neither the name of the copyright holder nor its contributors may be used
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
#include "magickey.h"

#if defined(LOSCFG_SHELL) && defined(LOSCFG_ENABLE_MAGICKEY)

#define MAGIC_KEY_CTRL_R 0x12 /* ctrl+r 开关 */
#define MAGIC_KEY_CTRL_T 0x14 /* ctrl+t 任务信息 */
#define MAGIC_KEY_CTRL_Z 0x1a /* ctrl+z 帮助 */
#define MAGIC_KEY_CTRL_E 0x05 /* ctrl+e 内存校验 */

static UINT32 Testcase(VOID)
{
    INT32 ret;

    /* F-Pos: ctrl+r 打开 magic key 开关,返回 1 表示按键被消费 */
    ret = CheckMagicKey(MAGIC_KEY_CTRL_R);
    ICUNIT_ASSERT_EQUAL(ret, 1, ret);

    /* F-Pos: 开关打开后 ctrl+z 打印帮助,按键被消费 */
    ret = CheckMagicKey(MAGIC_KEY_CTRL_Z);
    ICUNIT_ASSERT_EQUAL(ret, 1, ret);

    /* F-Pos: ctrl+t 打印任务信息,按键被消费 */
    ret = CheckMagicKey(MAGIC_KEY_CTRL_T);
    ICUNIT_ASSERT_EQUAL(ret, 1, ret);

    /* F-Pos: ctrl+e 执行内存完整性校验,按键被消费 */
    ret = CheckMagicKey(MAGIC_KEY_CTRL_E);
    ICUNIT_ASSERT_EQUAL(ret, 1, ret);

    /* F-Pos: ctrl+r 再次切换关闭开关(用例结束后恢复初始关闭态) */
    ret = CheckMagicKey(MAGIC_KEY_CTRL_R);
    ICUNIT_ASSERT_EQUAL(ret, 1, ret);

    /* F-Neg: 开关关闭后 magic key 不再消费按键 */
    ret = CheckMagicKey(MAGIC_KEY_CTRL_Z);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* F-Neg: 普通字符永远不是 magic key */
    ret = CheckMagicKey('a');
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    return LOS_OK;
}

/*
 * @tc.name     ItLosShell032
 * @tc.desc     验证 magic key: ctrl+r 开关切换消费按键; 开启后 ctrl+z/t/e
 *              帮助/任务信息/内存校验路径执行且按键被消费; 关闭后及普通
 *              字符不消费(ctrl+p 主动 panic 不在用例覆盖范围)
 */
VOID ItLosShell032(VOID)
{
    TEST_ADD_CASE("ItLosShell032", Testcase, TEST_LOS, TEST_FN_KERNELEXT_SHELL, TEST_LEVEL1, TEST_FUNCTION);
}

#endif /* LOSCFG_SHELL && LOSCFG_ENABLE_MAGICKEY */
