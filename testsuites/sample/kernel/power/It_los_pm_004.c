/*
 * Copyright (c) 2021-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 * of conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific prior written
 * permission.
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

#include "osTest.h"
#include "It_los_pm.h"

/*
 * 用例简要描述: PM 模式设置/查询/唤醒标志接口一揽子：LOS_PmModeGet 合法枚举契约、
 * LOS_PmModeSet 同值往返无漂移、非法 mode 与未注册 device/handler 缺失的拒绝分支、
 * LOS_PmWakeSet 冒烟。禁止真实进入 DEEP_SLEEP/SHUTDOWN 等破坏性模式：
 * - SHUTDOWN 仅在注册 device 后借默认 sysctrl 的 shutdownSuspend == NULL 触达
 *   HANDLER_NULL 拒绝分支（被拒绝，模式不改变，不会真正关机）；
 * - DEEP/LIGHT 的 HANDLER_NULL 分支在默认 sysctrl（deepSuspend/lightSuspend =
 *   ArchEnterSleep 非 NULL）下不可达，不做设置 DEEP_SLEEP 的破坏性尝试。
 */

static UINT32 DeviceSuspend(UINT32 mode)
{
    (VOID)mode;
    return LOS_OK;
}

static VOID DeviceResume(UINT32 mode)
{
    (VOID)mode;
    return;
}

static LosPmDevice g_pmDevice004 = {
    .suspend = DeviceSuspend,
    .resume = DeviceResume,
};

static UINT32 TestCase(VOID)
{
    UINT32 ret;
    LOS_SysSleepEnum curMode;

    /* F-Pos: LOS_PmModeGet 契约：返回值必须是合法枚举 [LOS_SYS_NORMAL_SLEEP, LOS_SYS_SHUTDOWN]。 */
    curMode = LOS_PmModeGet();
    ICUNIT_ASSERT_WITHIN_EQUAL(curMode, LOS_SYS_NORMAL_SLEEP, LOS_SYS_SHUTDOWN, curMode);

    /* F-Pos: NORMAL 模式无前置条件，永远允许设置；用于环境归一
     *（It_los_pm_001~003 收尾均已恢复正常态，此处显式确认）。 */
    ret = LOS_PmModeSet(LOS_SYS_NORMAL_SLEEP);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    curMode = LOS_PmModeGet();
    ICUNIT_ASSERT_EQUAL(curMode, LOS_SYS_NORMAL_SLEEP, curMode);

    /* F-Pos: 同值往返：先 Get 当前模式再 Set 同值，期望 LOS_OK 且无状态漂移。 */
    ret = LOS_PmModeSet(curMode);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    curMode = LOS_PmModeGet();
    ICUNIT_ASSERT_EQUAL(curMode, LOS_SYS_NORMAL_SLEEP, curMode);

    /* F-Inv: 非法 mode（越界正值 0x7F）→ LOS_ERRNO_PM_INVALID_MODE，模式不被改变。 */
    ret = LOS_PmModeSet((LOS_SysSleepEnum)0x7F);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_PM_INVALID_MODE, ret);
    curMode = LOS_PmModeGet();
    ICUNIT_ASSERT_EQUAL(curMode, LOS_SYS_NORMAL_SLEEP, curMode);

    /* F-Inv: 非法 mode（负值，(INT32)0xFFFFFFFF == -1）→ LOS_ERRNO_PM_INVALID_MODE。 */
    ret = LOS_PmModeSet((LOS_SysSleepEnum)0xFFFFFFFF);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_PM_INVALID_MODE, ret);
    curMode = LOS_PmModeGet();
    ICUNIT_ASSERT_EQUAL(curMode, LOS_SYS_NORMAL_SLEEP, curMode);

    /* F-Inv: 未注册 device 时设置非 NORMAL 模式 → LOS_ERRNO_PM_DEVICE_NULL。
     * 前置：It_los_pm_001~003 收尾均已 Unregister device。 */
    ret = LOS_PmModeSet(LOS_SYS_LIGHT_SLEEP);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_PM_DEVICE_NULL, ret);
    curMode = LOS_PmModeGet();
    ICUNIT_ASSERT_EQUAL(curMode, LOS_SYS_NORMAL_SLEEP, curMode);

    /* 注册最小 device 以继续触达需要 device 的分支。 */
    ret = LOS_PmRegister(LOS_PM_TYPE_DEVICE, &g_pmDevice004);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* F-Inv: SHUTDOWN 模式要求 sysctrl->shutdownSuspend 非 NULL，默认 sysctrl 下为 NULL
     * → LOS_ERRNO_PM_HANDLER_NULL（安全拒绝分支，不会真正关机，模式不改变）。 */
    ret = LOS_PmModeSet(LOS_SYS_SHUTDOWN);
    ICUNIT_GOTO_EQUAL(ret, LOS_ERRNO_PM_HANDLER_NULL, ret, EXIT);
    curMode = LOS_PmModeGet();
    ICUNIT_GOTO_EQUAL(curMode, LOS_SYS_NORMAL_SLEEP, curMode, EXIT);

    /* F-Pos: LOS_PmWakeSet 冒烟：仅设置内部唤醒标志（VOID 契约，无对外查询接口，
     * isWake 由下一次 suspend 流程自动清除），设置后让出 CPU 若干 tick，
     * 用例正常继续且模式无漂移即视为通过。 */
    LOS_PmWakeSet();
    ret = LOS_TaskDelay(2); /* 2 ticks: let idle task run a scheduling round */
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    curMode = LOS_PmModeGet();
    ICUNIT_GOTO_EQUAL(curMode, LOS_SYS_NORMAL_SLEEP, curMode, EXIT);

EXIT:
    /* 清理收尾：注销 device（OsPmDeviceUnregister 同时把模式恢复 NORMAL）并最终确认。 */
    ret = LOS_PmUnregister(LOS_PM_TYPE_DEVICE, &g_pmDevice004);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_PmModeSet(LOS_SYS_NORMAL_SLEEP);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    curMode = LOS_PmModeGet();
    ICUNIT_ASSERT_EQUAL(curMode, LOS_SYS_NORMAL_SLEEP, curMode);
    return LOS_OK;
}

VOID ItLosPm004(VOID) // IT_Layer_ModuleORFeature_No
{
    TEST_ADD_CASE("ItLosPm004", TestCase, TEST_LOS, TEST_PM, TEST_LEVEL0, TEST_FUNCTION);
}
