/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided the following conditions are met:
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
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
#include "It_los_lms.h"

/* 用例简要描述: 仅用对外接口覆盖 libc 拦截检查路径与 noreturn 插桩回调 ——
 * 对注册检查池memset/memcpy 投毒区域触发 STORE/LOAD 错误报告(经拦截器
 * storeN/loadN 检查); 字符串函数 memset/memcpy/memmove/strcpy/strcat/
 * strncpy/strncat 的正常与空指针参数路径; 子任务通过声明为 noreturn 的
 * 自删除函数退出, 编译器在该调用点自然插入并执行 __asan_handle_no_return
 * 回调(noabort 空实现), 父任务用信号量同步(带超时)确认子任务已退出。 */

#define LMS036_POOL_SIZE 512

static UINT64 g_lms036Pool[LMS036_POOL_SIZE / sizeof(UINT64)];
static CHAR g_lms036Dst[64]; /* 64: 拦截器正常路径目标缓冲 */

static UINT32 g_lms036SemId;
static volatile UINT32 g_lms036TaskDone;

/* 自删除函数: LOS_TaskDelete(当前任务) 成功后不返回; 兜底延迟等待回收 */
STATIC VOID Lms036TaskExit(VOID) __attribute__((noreturn));
STATIC VOID Lms036TaskExit(VOID)
{
    (VOID)LOS_TaskDelete(LOS_CurTaskIDGet());
    for (;;) {
        (VOID)LOS_TaskDelay(10); /* 10: 兜底, 正常不达 */
    }
}

STATIC VOID Lms036TaskEntry(UINT32 param)
{
    (VOID)param;
    g_lms036TaskDone++;
    (VOID)LOS_SemPost(g_lms036SemId);
    Lms036TaskExit(); /* noreturn 调用点: 编译器插入 __asan_handle_no_return */
}

static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT32 realSize;
    UINT32 taskId;
    TSK_INIT_PARAM_S taskParam = {0};

    realSize = LOS_LmsAddCheckPool(g_lms036Pool, LMS036_POOL_SIZE);
    ICUNIT_GOTO_NOT_EQUAL(realSize, 0, realSize, EXIT);

    /* 投毒区域(新注册池影子为 AFTERFREE)的拦截检查: STORE/LOAD 错误报告 */
    (VOID)memset((VOID *)((UINTPTR)g_lms036Pool + 64), 0xAA, 32); /* 64/32: 池内投毒区 */
    LMS_FEED_WDT(); /* 每份报告后喂狗 */
    (VOID)memcpy(g_lms036Dst, (VOID *)((UINTPTR)g_lms036Pool + 128), 32); /* 128/32: 投毒源 */
    LMS_FEED_WDT();

    /* 拦截器正常路径(目标不在检查表, 无报告) */
    (VOID)memset(g_lms036Dst, 0, sizeof(g_lms036Dst));
    (VOID)memcpy(g_lms036Dst, "hello", 6); /* 6: 含结尾符 */
    (VOID)memmove(g_lms036Dst + 1, g_lms036Dst, 4); /* 4: 重叠区搬运 */
    (VOID)strcpy(g_lms036Dst + 16, "abc"); /* 16: 目标偏移 */
    (VOID)strcat(g_lms036Dst + 16, "def");
    (VOID)strncpy(g_lms036Dst + 32, "xyz", 8); /* 32/8: 目标偏移/长度 */
    (VOID)strncat(g_lms036Dst + 32, "uvw", 3); /* 3: 追加长度 */

    /* 拦截器空指针参数路径: 安全返回 NULL */
    ICUNIT_GOTO_EQUAL((UINTPTR)strcpy(NULL, "a"), (UINTPTR)NULL, (UINTPTR)NULL, EXIT);
    ICUNIT_GOTO_EQUAL((UINTPTR)strcat(NULL, "a"), (UINTPTR)NULL, (UINTPTR)NULL, EXIT);
    ICUNIT_GOTO_EQUAL((UINTPTR)strncpy(NULL, "a", 1), (UINTPTR)NULL, (UINTPTR)NULL, EXIT);
    ICUNIT_GOTO_EQUAL((UINTPTR)strncat(NULL, "a", 1), (UINTPTR)NULL, (UINTPTR)NULL, EXIT);
    ICUNIT_GOTO_EQUAL((UINTPTR)strcpy(g_lms036Dst, NULL), (UINTPTR)NULL, (UINTPTR)NULL, EXIT);
    ICUNIT_GOTO_EQUAL((UINTPTR)strcat(g_lms036Dst, NULL), (UINTPTR)NULL, (UINTPTR)NULL, EXIT);
    ICUNIT_GOTO_EQUAL((UINTPTR)strncpy(g_lms036Dst, NULL, 1), (UINTPTR)NULL, (UINTPTR)NULL, EXIT);
    ICUNIT_GOTO_EQUAL((UINTPTR)strncat(g_lms036Dst, NULL, 1), (UINTPTR)NULL, (UINTPTR)NULL, EXIT);

    /* noreturn 子任务: 触发编译器插入的 __asan_handle_no_return 回调 */
    g_lms036TaskDone = 0;
    ret = LOS_SemCreate(0, &g_lms036SemId);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)Lms036TaskEntry;
    taskParam.usTaskPrio = 10; /* 10: 普通优先级 */
    taskParam.pcName = "Lms036Task";
    taskParam.uwStackSize = 0x800; /* 0x800: 任务栈 */
    taskParam.uwResved = LOS_TASK_ATTR_JOINABLE;
    ret = LOS_TaskCreate(&taskId, &taskParam);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT_DELSEM);

    ret = LOS_SemPend(g_lms036SemId, 100); /* 100: 同步超时 tick */
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT_DELSEM);
    ICUNIT_GOTO_NOT_EQUAL(g_lms036TaskDone, 0, g_lms036TaskDone, EXIT_DELSEM);

    /* 回收 Joinable 任务资源(任务栈), 避免资源泄漏 */
    ret = LOS_TaskJoin(taskId, NULL);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT_DELSEM);

EXIT_DELSEM:
    (VOID)LOS_SemDelete(g_lms036SemId);
EXIT:
    LOS_LmsDelCheckPool(g_lms036Pool);
    return LOS_OK;
}

VOID ItLosLms036(void)
{
    TEST_ADD_CASE("ItLosLms036", TestCase, TEST_LOS, TEST_LMS, TEST_LEVEL1, TEST_FUNCTION);
}
