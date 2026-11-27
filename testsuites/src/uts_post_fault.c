/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2026 Huawei Device Co., Ltd. All rights reserved.
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

/*
 * UTS post-fault kernel checks - LOSCFG_TEST_BOX_FAULT builds only.
 *
 * Run by the kernel coordinator (osTest.c) after a box was destroyed for a
 * fault. Each case exercises, from kernel mode, one service a running system
 * depends on, so "the kernel keeps running" is backed by behaviour and not
 * only by the coordinator printing a summary.
 */
#include "osTest.h"
#include "los_task.h"
#include "los_sem.h"
#include "los_mux.h"
#include "los_queue.h"
#include "los_event.h"
#include "los_swtmr.h"
#include "los_tick.h"
#include "los_memory.h"
#include "los_box_pri.h"
#include "uts_box_fault.h"

#ifdef LOSCFG_TEST_BOX_FAULT

#define UTS_PF_STACK    0x400U
#define UTS_PF_PRIO     2U  /* above the coordinator: runs as soon as it is created */
#define UTS_PF_WAIT     5U

static volatile UINT32 g_utsPfFlag;
static volatile UINT32 g_utsPfCount;
static UINT32 g_utsPfTask;
static UINT32 g_utsPfSem;

/* Higher priority than the creator: sets the flag and parks. */
static VOID UtsPfWorker(UINT32 arg)
{
    (VOID)arg;
    g_utsPfFlag = 1U;
    (VOID)LOS_TaskDelay(LOSCFG_BASE_CORE_TICK_PER_SECOND * 10U);
}

/* Software timers run in the timer task: it preempts any waiting task. */
static VOID UtsPfTimerPost(UINTPTR arg)
{
    (VOID)LOS_SemPost(*(volatile UINT32 *)(UINTPTR)arg);
}

static VOID UtsPfTimerCount(UINTPTR arg)
{
    *(volatile UINT32 *)(UINTPTR)arg += 1U;
}

static UINT32 UtsPfCreateTask(TSK_ENTRY_FUNC entry)
{
    TSK_INIT_PARAM_S param = {0};

    param.pfnTaskEntry = entry;
    param.usTaskPrio = UTS_PF_PRIO;
    param.pcName = "UtsPfWorker";
    param.uwStackSize = UTS_PF_STACK;
    return LOS_TaskCreate(&g_utsPfTask, &param);
}

static UINT32 UtsPfCaseTask(VOID)
{
    UINT32 ret;

    g_utsPfFlag = 0U;
    ret = UtsPfCreateTask((TSK_ENTRY_FUNC)UtsPfWorker);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    LOS_TaskDelay(UTS_PF_WAIT);
    ICUNIT_ASSERT_EQUAL(g_utsPfFlag, 1U, g_utsPfFlag);

    ret = LOS_TaskDelete(g_utsPfTask);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    return LOS_OK;
}

static UINT32 UtsPfCaseSem(VOID)
{
    UINT32 ret;
    UINT32 timer = 0U;

    ret = LOS_SemCreate(0U, &g_utsPfSem);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* This task blocks on the semaphore and is woken by the timer. */
    ret = LOS_SwtmrCreate(UTS_PF_WAIT, LOS_SWTMR_MODE_ONCE, (SWTMR_PROC_FUNC)UtsPfTimerPost, &timer,
                          (UINTPTR)&g_utsPfSem);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_SwtmrStart(timer);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ret = LOS_SemPend(g_utsPfSem, UTS_PF_WAIT * 20U);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ret = LOS_SemPend(g_utsPfSem, UTS_PF_WAIT);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_SEM_TIMEOUT, ret);

    ret = LOS_SemDelete(g_utsPfSem);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    return LOS_OK;
}

static UINT32 UtsPfCaseMux(VOID)
{
    UINT32 mux = 0U;
    UINT32 ret;

    ret = LOS_MuxCreate(&mux);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_MuxPend(mux, LOS_WAIT_FOREVER);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_MuxPost(mux);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_MuxDelete(mux);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    return LOS_OK;
}

static UINT32 UtsPfCaseQueue(VOID)
{
    UINT32 queue = 0U;
    UINT32 sent = 0x13572468U;
    UINT32 got = 0U;
    UINT32 size;
    UINT32 ret;

    ret = LOS_QueueCreate("UtsPfQueue", 4U, &queue, 0U, sizeof(UINT32));
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    size = sizeof(UINT32);
    ret = LOS_QueueWriteCopy(queue, &sent, size, LOS_NO_WAIT);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    size = sizeof(UINT32);
    ret = LOS_QueueReadCopy(queue, &got, &size, LOS_NO_WAIT);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ICUNIT_ASSERT_EQUAL(got, sent, got);
    ret = LOS_QueueDelete(queue);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    return LOS_OK;
}

static UINT32 UtsPfCaseEvent(VOID)
{
    EVENT_CB_S event = {0};
    UINT32 ret;

    ret = LOS_EventInit(&event);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* Nobody posts: the timed read must come back by the tick. */
    ret = LOS_EventRead(&event, 0x1U, LOS_WAITMODE_AND, UTS_PF_WAIT);
    ICUNIT_ASSERT_EQUAL(ret, (UINT32)LOS_ERRNO_EVENT_READ_TIMEOUT, ret);

    ret = LOS_EventWrite(&event, 0x1U);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_EventRead(&event, 0x1U, LOS_WAITMODE_AND, LOS_NO_WAIT);
    ICUNIT_ASSERT_EQUAL(ret, 0x1U, ret);

    ret = LOS_EventDestroy(&event);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    return LOS_OK;
}

static UINT32 UtsPfCaseSwtmr(VOID)
{
    UINT32 timer = 0U;
    UINT32 ret;
    UINT32 i;

    g_utsPfCount = 0U;
    /* Not self-deleting: the timer must still exist afterwards to be deleted. */
    ret = LOS_SwtmrCreate(UTS_PF_WAIT, LOS_SWTMR_MODE_NO_SELFDELETE, (SWTMR_PROC_FUNC)UtsPfTimerCount,
                          &timer, (UINTPTR)&g_utsPfCount);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_SwtmrStart(timer);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    for (i = 0U; (g_utsPfCount == 0U) && (i < (UTS_PF_WAIT * 20U)); i++) {
        LOS_TaskDelay(1U);
    }
    ICUNIT_ASSERT_EQUAL(g_utsPfCount, 1U, g_utsPfCount);

    ret = LOS_SwtmrDelete(timer);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    return LOS_OK;
}

static UINT32 UtsPfCaseMem(VOID)
{
    VOID *block[3] = {NULL, NULL, NULL};
    UINT32 size[3] = {32U, 256U, 1024U};
    UINT32 ret;
    UINT32 i;

    for (i = 0U; i < 3U; i++) {
        block[i] = LOS_MemAlloc((VOID *)LOSCFG_SYS_HEAP_ADDR, size[i]);
        ICUNIT_ASSERT_NOT_EQUAL((UINTPTR)block[i], (UINTPTR)NULL, i);
        *(volatile UINT32 *)block[i] = i + 1U;
    }
    /* Free out of order: the allocator must merge what it is given. */
    for (i = 3U; i > 0U; i--) {
        ret = LOS_MemFree((VOID *)LOSCFG_SYS_HEAP_ADDR, block[i - 1U]);
        ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    }
    return LOS_OK;
}

static UINT32 UtsPfCaseTick(VOID)
{
    UINT64 start;
    UINT32 ret;

    start = LOS_TickCountGet();
    ret = LOS_TaskDelay(UTS_PF_WAIT);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    /* The tick that wakes this task can be counted just after the task is
     * scheduled, so a delay of N ticks legitimately measures N-1. */
    ICUNIT_ASSERT_WITHIN_EQUAL((UINT32)(LOS_TickCountGet() - start), UTS_PF_WAIT - 1U, 0xFFFFFFFFU,
                               (UINT32)(LOS_TickCountGet() - start));
    return LOS_OK;
}

static UINT32 UtsPfCaseBoxState(VOID)
{
    /* Exactly what the fault should have left behind: one box down, the
     * other one up, and this kernel task in kernel context. */
    ICUNIT_ASSERT_EQUAL(BOX_IS_UNINIT(g_boxCBArr[UTS_FAULT_BOX].boxStatus) ? 1U : 0U, 1U,
                        g_boxCBArr[UTS_FAULT_BOX].boxStatus);
    ICUNIT_ASSERT_EQUAL(BOX_IS_INIT(g_boxCBArr[UTS_SURVIVOR_BOX].boxStatus) ? 1U : 0U, 1U,
                        g_boxCBArr[UTS_SURVIVOR_BOX].boxStatus);
    ICUNIT_ASSERT_EQUAL((UINTPTR)OsGetCurBoxCB(), (UINTPTR)NULL, g_curBoxId);
    ICUNIT_ASSERT_EQUAL(g_curBoxId, OS_KERNEL_BOX, g_curBoxId);
    return LOS_OK;
}

VOID UtsPostFaultKernelCheck(VOID)
{
    dprintf("\t\n --- UTS post-fault kernel checks --- \n\n");
    TEST_ADD_CASE("UtsPfBoxState", UtsPfCaseBoxState, TEST_LOS, TEST_TASK, TEST_LEVEL1, TEST_FUNCTION);
    TEST_ADD_CASE("UtsPfTask", UtsPfCaseTask, TEST_LOS, TEST_TASK, TEST_LEVEL1, TEST_FUNCTION);
    TEST_ADD_CASE("UtsPfSem", UtsPfCaseSem, TEST_LOS, TEST_SEM, TEST_LEVEL1, TEST_FUNCTION);
    TEST_ADD_CASE("UtsPfMux", UtsPfCaseMux, TEST_LOS, TEST_MUX, TEST_LEVEL1, TEST_FUNCTION);
    TEST_ADD_CASE("UtsPfQueue", UtsPfCaseQueue, TEST_LOS, TEST_QUE, TEST_LEVEL1, TEST_FUNCTION);
    TEST_ADD_CASE("UtsPfEvent", UtsPfCaseEvent, TEST_LOS, TEST_EVENT, TEST_LEVEL1, TEST_FUNCTION);
    TEST_ADD_CASE("UtsPfSwtmr", UtsPfCaseSwtmr, TEST_LOS, TEST_SWTMR, TEST_LEVEL1, TEST_FUNCTION);
    TEST_ADD_CASE("UtsPfMem", UtsPfCaseMem, TEST_LOS, TEST_MEM, TEST_LEVEL1, TEST_FUNCTION);
    TEST_ADD_CASE("UtsPfTick", UtsPfCaseTick, TEST_LOS, TEST_TASK, TEST_LEVEL1, TEST_FUNCTION);
    dprintf("--- UTS post-fault kernel checks done ---\n");
}

#endif /* LOSCFG_TEST_BOX_FAULT */
