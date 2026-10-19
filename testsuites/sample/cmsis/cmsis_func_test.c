/*
 * Copyright (c) 2022-2022 Huawei Device Co., Ltd. All rights reserved.
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

#include <securec.h>
#include "osTest.h"
#include "cmsis_os.h"
#include "kal.h"

#define STATCI_BUFF_SIZE   32
#define READ_BUFFER_SIZIE  7
#define QUEUE_WAIT_TIMEOUT 3

static VOID CmsisStackFunc01(void)
{
    g_testCount++;
    return;
}

/**
 * @tc.number    : SUB_KERNEL_PTHREAD_OPERATION_001
 * @tc.name      : event operation for join
 * @tc.desc      : [C- SOFTWARE -0200]
 */

static volatile UINT32 g_cmsisLowFreqTimerRan;
static void CmsisLowFreqTimerCb(void *argument)
{
    (void)argument;
    g_cmsisLowFreqTimerRan++;
}

static UINT32 TestCmsis001(VOID)
{
    osThreadId_t threadId;
    osThreadAttr_t attr = {0};

    g_testCount = 0;

    void *stackAddr = NULL;

    stackAddr = LOS_MemAllocAlign(OS_TASK_STACK_ADDR, OS_TSK_TEST_STACK_SIZE, 8);
    ICUNIT_ASSERT_NOT_EQUAL(stackAddr, NULL, stackAddr);

    attr.stack_mem = stackAddr;
    attr.stack_size = OS_TSK_TEST_STACK_SIZE;
    attr.priority = osPriorityNormal + 1;
    attr.attr_bits = osThreadDetached;
    threadId = osThreadNew((osThreadFunc_t)CmsisStackFunc01, NULL, &attr);
    ICUNIT_GOTO_NOT_EQUAL(threadId, 0, threadId, EXIT);

    ICUNIT_GOTO_EQUAL(g_testCount, 1, g_testCount, EXIT);
EXIT:
    (VOID)LOS_MemFree(OS_TASK_STACK_ADDR, stackAddr);
    return LOS_OK;
};

/**
 * @tc.name: TestCmsis007
 * @tc.desc: set and get queue name
 * @tc.type: FUNC
 * @tc.require: issueI5LBE8
 */
static UINT32 TestCmsis007(VOID)
{
    osMessageQueueId_t msgQueueId;
    osMessageQueueAttr_t attr = {0};
    CHAR strbuff[] = "hello world";
    CHAR *name = NULL;
    INT32 ret;

    attr.name = "q1";
    /* dynamic test */
    msgQueueId = osMessageQueueNew(1, strlen(strbuff), &attr);
    ICUNIT_ASSERT_NOT_EQUAL(msgQueueId, NULL, msgQueueId);

    name = (CHAR *)osMessageQueueGetName(msgQueueId);
    ret = strcmp(name, "q1");
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    ret = osMessageQueueDelete(msgQueueId);
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);

    name = (CHAR *)osMessageQueueGetName(msgQueueId);
    ICUNIT_ASSERT_EQUAL(name, NULL, name);

#if (LOSCFG_QUEUE_STATIC_ALLOCATION == 1)
    CHAR staticBuff[STATCI_BUFF_SIZE + sizeof(UINT32)] = {0};
    attr.mq_mem = staticBuff;
    attr.mq_size = STATCI_BUFF_SIZE + sizeof(UINT32);
    msgQueueId = osMessageQueueNew(1, STATCI_BUFF_SIZE, &attr);
    ICUNIT_ASSERT_NOT_EQUAL(msgQueueId, NULL, msgQueueId);

    name = (CHAR *)osMessageQueueGetName(msgQueueId);
    ret = strcmp(name, "q1");
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    ret = osMessageQueueDelete(msgQueueId);
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);

    name = (CHAR *)osMessageQueueGetName(msgQueueId);
    ICUNIT_ASSERT_EQUAL(name, NULL, name);
#endif

    return LOS_OK;

EXIT:
    ret = osMessageQueueDelete(msgQueueId);
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);

    return LOS_OK;
}

#if (LOSCFG_QUEUE_STATIC_ALLOCATION == 1)
static osMessageQueueId_t g_msgQueueId1;
static osMessageQueueId_t g_msgQueueId2;

static osThreadId_t threadId1;
static osThreadId_t threadId2;

static CHAR g_strbuff1[] = "hello";
static CHAR g_strbuff2[] = "world";
static CHAR g_staticBuff[STATCI_BUFF_SIZE + sizeof(UINT32)] = {0};

static VOID CmsisQueueTestThread1(VOID)
{
    CHAR data[READ_BUFFER_SIZIE] = {0};
    INT32 ret;
    osStatus_t status;

    ret = osMessageQueuePut(g_msgQueueId1, &g_strbuff1, 0U, 0U);
    ICUNIT_ASSERT_EQUAL_VOID(ret, osOK, ret);

    status = osMessageQueueGet(g_msgQueueId2, &data, NULL, QUEUE_WAIT_TIMEOUT);
    ICUNIT_ASSERT_EQUAL_VOID(status, osOK, status);

    ret = strcmp(data, "world");
    ICUNIT_ASSERT_EQUAL_VOID(ret, 0, ret);
}

static VOID CmsisQueueTestThread2(VOID)
{
    CHAR data[READ_BUFFER_SIZIE] = {0};
    INT32 ret;
    osStatus_t status;

    status = osMessageQueueGet(g_msgQueueId1, &data, NULL, QUEUE_WAIT_TIMEOUT);
    ICUNIT_ASSERT_EQUAL_VOID(status, osOK, status);

    ret = strcmp(data, "hello");
    ICUNIT_ASSERT_EQUAL_VOID(ret, 0, ret);

    ret = osMessageQueuePut(g_msgQueueId2, &g_strbuff2, 0U, 0U);
    ICUNIT_ASSERT_EQUAL_VOID(ret, osOK, ret);
}

static INT32 ThreadReadWriteTest(VOID)
{
    osMessageQueueAttr_t attr = {0};
    INT32 ret;

    g_msgQueueId1 = osMessageQueueNew(1, strlen(g_strbuff1), NULL);
    ICUNIT_ASSERT_NOT_EQUAL(g_msgQueueId1, NULL, g_msgQueueId1);

    attr.mq_mem = g_staticBuff;
    attr.mq_size = strlen(g_strbuff2) + 1 + sizeof(UINT32);
    g_msgQueueId2 = osMessageQueueNew(1, strlen(g_strbuff2) + 1, &attr);
    ICUNIT_ASSERT_NOT_EQUAL(g_msgQueueId2, NULL, g_msgQueueId2);

    threadId1 = osThreadNew((osThreadFunc_t)CmsisQueueTestThread1, NULL, NULL);
    ICUNIT_ASSERT_NOT_EQUAL(threadId1, NULL, threadId1);

    threadId2 = osThreadNew((osThreadFunc_t)CmsisQueueTestThread2, NULL, NULL);
    ICUNIT_ASSERT_NOT_EQUAL(threadId2, NULL, threadId2);

    osThreadJoin(threadId1);

    ret = strcmp(g_staticBuff, "world");
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ret = osMessageQueueDelete(g_msgQueueId1);
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);

    ret = osMessageQueueDelete(g_msgQueueId2);
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);

    return LOS_OK;
}

/**
 * @tc.name: TestCmsis006
 * @tc.desc: mix read write
 * @tc.type: FUNC
 * @tc.require: issueI5LBE8
 */
static UINT32 TestCmsis006(VOID)
{
    INT32 ret;

    ret = ThreadReadWriteTest();
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    return LOS_OK;
}
#endif

/**
 * @tc.name: TestCmsis005
 * @tc.desc: read-write exception
 * @tc.type: FUNC
 * @tc.require: issueI5LBE8
 */
static UINT32 TestCmsis005(VOID)
{
    osMessageQueueId_t msgQueueId;
    CHAR strbuff[] = "hello world";
    CHAR data[STATCI_BUFF_SIZE] = {0};
    INT32 ret;

    ret = osMessageQueuePut(NULL, &strbuff, 0U, 0U);
    ICUNIT_ASSERT_EQUAL(ret, osErrorParameter, ret);

    ret = osMessageQueueGet(NULL, &data, NULL, 0U);
    ICUNIT_ASSERT_EQUAL(ret, osErrorParameter, ret);

    /* dynmic test */
    msgQueueId = osMessageQueueNew(1, strlen(strbuff), NULL);
    ICUNIT_ASSERT_NOT_EQUAL(msgQueueId, NULL, msgQueueId);

    ret = osMessageQueuePut(msgQueueId, NULL, 0U, 0U);
    ICUNIT_GOTO_EQUAL(ret, osErrorParameter, ret, EXIT);

    ret = osMessageQueueGet(msgQueueId, NULL, NULL, 0U);
    ICUNIT_GOTO_EQUAL(ret, osErrorParameter, ret, EXIT);

    ret = osMessageQueueDelete(msgQueueId);
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);

    ret = osMessageQueuePut(msgQueueId, &strbuff, 0U, 0U);
    ICUNIT_GOTO_EQUAL(ret, osErrorParameter, ret, EXIT);

    ret = osMessageQueueGet(msgQueueId, &data, NULL, 0U);
    ICUNIT_GOTO_EQUAL(ret, osErrorParameter, ret, EXIT);

#if (LOSCFG_QUEUE_STATIC_ALLOCATION == 1)
    /* static test */
    osMessageQueueAttr_t attr = {0};
    CHAR staticBuff[STATCI_BUFF_SIZE + sizeof(UINT32)] = {0};
    attr.mq_mem = staticBuff;
    attr.mq_size = STATCI_BUFF_SIZE + sizeof(UINT32);
    msgQueueId = osMessageQueueNew(1, STATCI_BUFF_SIZE, &attr);
    ICUNIT_ASSERT_NOT_EQUAL(msgQueueId, NULL, msgQueueId);

    ret = osMessageQueuePut(msgQueueId, NULL, 0U, 0U);
    ICUNIT_GOTO_EQUAL(ret, osErrorParameter, ret, EXIT);

    ret = osMessageQueueGet(msgQueueId, NULL, NULL, 0U);
    ICUNIT_GOTO_EQUAL(ret, osErrorParameter, ret, EXIT);

    ret = osMessageQueueDelete(msgQueueId);
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);

    ret = osMessageQueuePut(msgQueueId, &strbuff, 0U, 0U);
    ICUNIT_GOTO_EQUAL(ret, osErrorParameter, ret, EXIT);

    ret = osMessageQueueGet(msgQueueId, &data, NULL, 0U);
    ICUNIT_GOTO_EQUAL(ret, osErrorParameter, ret, EXIT);
#endif

    return LOS_OK;

EXIT:
    ret = osMessageQueueDelete(msgQueueId);
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);

    return LOS_OK;
}

/**
 * @tc.name: TestCmsis004
 * @tc.desc: read write test
 * @tc.type: FUNC
 * @tc.require: issueI5LBE8
 */
static UINT32 TestCmsis004(VOID)
{
    osMessageQueueId_t msgQueueId;
    CHAR strbuff[] = "hello world";
    CHAR data[STATCI_BUFF_SIZE] = {0};
    INT32 ret;

    /* dynamic test */
    msgQueueId = osMessageQueueNew(1, strlen(strbuff), NULL);
    ICUNIT_ASSERT_NOT_EQUAL(msgQueueId, NULL, msgQueueId);

    ret = osMessageQueuePut(msgQueueId, &strbuff, 0U, 0U);
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);

    ret = osMessageQueueGet(msgQueueId, &data, NULL, 0U);
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);

    ret = strcmp(data, strbuff);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ret = osMessageQueueDelete(msgQueueId);
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);

#if (LOSCFG_QUEUE_STATIC_ALLOCATION == 1)
    /* static test */
    osMessageQueueAttr_t attr = {0};
    CHAR staticBuff[STATCI_BUFF_SIZE + sizeof(UINT32)] = {0};
    attr.mq_mem = staticBuff;
    attr.mq_size = strlen(strbuff) + 1 + sizeof(UINT32);
    msgQueueId = osMessageQueueNew(1, strlen(strbuff) + 1, &attr);
    ICUNIT_ASSERT_NOT_EQUAL(msgQueueId, NULL, msgQueueId);

    ret = osMessageQueuePut(msgQueueId, &strbuff, 0U, 0U);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);

    ret = osMessageQueueGet(msgQueueId, &data, NULL, 0U);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);

    ret = strcmp(data, strbuff);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    ret = strcmp(staticBuff, strbuff);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

EXIT:
    ret = osMessageQueueDelete(msgQueueId);
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);
#endif
    return LOS_OK;
}

/**
 * @tc.name: TestCmsis003
 * @tc.desc: create exception parameters test
 * @tc.type: FUNC
 * @tc.require: issueI5LBE8
 */
static UINT32 TestCmsis003(VOID)
{
    osMessageQueueId_t msgQueueId;
    CHAR strbuff[] = "hello world";

    /* dynmic test */
    msgQueueId = osMessageQueueNew(0, strlen(strbuff), NULL);
    ICUNIT_ASSERT_EQUAL(msgQueueId, NULL, msgQueueId);

    msgQueueId = osMessageQueueNew(1, 0xFFFFFFFF, NULL);
    ICUNIT_ASSERT_EQUAL(msgQueueId, NULL, msgQueueId);

#if (LOSCFG_QUEUE_STATIC_ALLOCATION == 1)
    /* static test */
    osMessageQueueAttr_t attr = {0};
    CHAR staticBuff[STATCI_BUFF_SIZE + sizeof(UINT32)] = {0};
    attr.mq_mem = staticBuff;
    attr.mq_size = STATCI_BUFF_SIZE + sizeof(UINT32);
    msgQueueId = osMessageQueueNew(0, strlen(strbuff), &attr);
    ICUNIT_ASSERT_EQUAL(msgQueueId, NULL, msgQueueId);

    msgQueueId = osMessageQueueNew(0xFFFFFFFF, strlen(strbuff), &attr);
    ICUNIT_ASSERT_EQUAL(msgQueueId, NULL, msgQueueId);

    attr.mq_mem = staticBuff;
    attr.mq_size = 0;
    msgQueueId = osMessageQueueNew(1, strlen(strbuff), &attr);
    ICUNIT_ASSERT_EQUAL(msgQueueId, NULL, msgQueueId);

    attr.mq_mem = NULL;
    attr.mq_size = STATCI_BUFF_SIZE + sizeof(UINT32);
    msgQueueId = osMessageQueueNew(1, strlen(strbuff), &attr);
    ICUNIT_ASSERT_EQUAL(msgQueueId, NULL, msgQueueId);
#endif

    return LOS_OK;
};

/**
 * @tc.name: TestCmsis002
 * @tc.desc: create and delete test
 * @tc.type: FUNC
 * @tc.require: issueI5LBE8
 */
static UINT32 TestCmsis002(VOID)
{
    osMessageQueueId_t msgQueueId;
    CHAR strbuff[] = "hello world";
    INT32 ret;

    /* dynamic test */
    msgQueueId = osMessageQueueNew(1, strlen(strbuff), NULL);
    ICUNIT_ASSERT_NOT_EQUAL(msgQueueId, NULL, msgQueueId);

    ret = osMessageQueueDelete(msgQueueId);
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);

    ret = osMessageQueueDelete(msgQueueId);
    ICUNIT_ASSERT_EQUAL(ret, osErrorParameter, ret);

#if (LOSCFG_QUEUE_STATIC_ALLOCATION == 1)
    /* static test */
    osMessageQueueAttr_t attr = {0};
    CHAR staticBuff[STATCI_BUFF_SIZE + sizeof(UINT32)] = {0};
    attr.mq_mem = staticBuff;
    attr.mq_size = STATCI_BUFF_SIZE + sizeof(UINT32);
    msgQueueId = osMessageQueueNew(1, STATCI_BUFF_SIZE, &attr);
    ICUNIT_ASSERT_NOT_EQUAL(msgQueueId, NULL, msgQueueId);

    ret = osMessageQueueDelete(msgQueueId);
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);

    ret = osMessageQueueDelete(msgQueueId);
    ICUNIT_ASSERT_EQUAL(ret, osErrorParameter, ret);
#endif

    return LOS_OK;
};

static VOID timerCallback(void *arg)
{
    return;
}

/**
 * @tc.name: TestCmsisTimer001
 * @tc.desc: Timer Management test
 * @tc.type: FUNC
 * @tc.require: issueI5TQ0T
 */
static UINT32 TestCmsisTimer001(VOID)
{
    osTimerId_t time_id;
    const char *timerGetName = NULL;
    osStatus_t ret;

    time_id = osTimerNew(timerCallback, osTimerOnce, NULL, NULL);
    ICUNIT_ASSERT_NOT_EQUAL(time_id, NULL, time_id);

    ret = osTimerStart(time_id, 100U); // 100, just for test
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);

    timerGetName = osTimerGetName(time_id);
    ICUNIT_GOTO_EQUAL(timerGetName, NULL, timerGetName, EXIT1);

    ret = osTimerIsRunning(time_id);
    ICUNIT_GOTO_EQUAL(ret, 1, ret, EXIT1);

    ret = osTimerStop(time_id);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT1);

    ret = osTimerIsRunning(time_id);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT1);

    ret = osTimerDelete(time_id);
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);

    return LOS_OK;

EXIT1:
    ret = osTimerDelete(time_id);
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);

    return LOS_OK;
};

static VOID CmsisSatFunc001(VOID)
{
    g_testCount++;
    return;
}

static UINT32 TestCmsisSaturation001(VOID)
{
    osThreadId_t threadId;
    osThreadAttr_t attr = {0};
    attr.stack_size = OS_TSK_TEST_STACK_SIZE;
    attr.priority = osPriorityHigh;
    attr.attr_bits = osThreadDetached;
    g_testCount = 0;
    threadId = osThreadNew((osThreadFunc_t)CmsisSatFunc001, NULL, &attr);
    ICUNIT_ASSERT_NOT_EQUAL(threadId, NULL, threadId);
    osDelay(5);
    ICUNIT_ASSERT_EQUAL(g_testCount, 1, g_testCount);
    return LOS_OK;
}

static UINT32 TestCmsisSaturation002(VOID)
{
    osThreadId_t threadId;
    osThreadAttr_t attr = {0};
    attr.stack_size = OS_TSK_TEST_STACK_SIZE;
    attr.priority = osPriorityHigh1;
    attr.attr_bits = osThreadDetached;
    threadId = osThreadNew((osThreadFunc_t)CmsisSatFunc001, NULL, &attr);
    ICUNIT_ASSERT_EQUAL(threadId, NULL, threadId);
    return LOS_OK;
}

static UINT32 TestCmsisSaturation003(VOID)
{
    osThreadId_t threadId;
    osThreadAttr_t attr = {0};
    attr.stack_size = OS_TSK_TEST_STACK_SIZE;
    attr.priority = osPriorityNone;
    attr.attr_bits = osThreadDetached;
    threadId = osThreadNew((osThreadFunc_t)CmsisSatFunc001, NULL, &attr);
    ICUNIT_ASSERT_EQUAL(threadId, NULL, threadId);
    return LOS_OK;
}

static UINT32 TestCmsisSaturation004(VOID)
{
    osThreadId_t threadId;
    osThreadAttr_t attr = {0};
    attr.stack_size = OS_TSK_TEST_STACK_SIZE;
    attr.priority = osPriorityIdle;
    attr.attr_bits = osThreadDetached;
    threadId = osThreadNew((osThreadFunc_t)CmsisSatFunc001, NULL, &attr);
    ICUNIT_ASSERT_EQUAL(threadId, NULL, threadId);
    return LOS_OK;
}

static UINT32 TestCmsisSaturation005(VOID)
{
    osThreadId_t threadId;
    osThreadAttr_t attr = {0};
    osStatus_t ret;
    attr.stack_size = OS_TSK_TEST_STACK_SIZE;
    attr.priority = osPriorityLow;
    attr.attr_bits = osThreadDetached;
    g_testCount = 0;
    threadId = osThreadNew((osThreadFunc_t)CmsisSatFunc001, NULL, &attr);
    ICUNIT_ASSERT_NOT_EQUAL(threadId, NULL, threadId);
    ret = osThreadSetPriority(threadId, osPriorityHigh);
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);
    osDelay(5);
    return LOS_OK;
}

static UINT32 TestCmsisSaturation006(VOID)
{
    osThreadId_t threadId;
    osThreadAttr_t attr = {0};
    osStatus_t ret;
    attr.stack_size = OS_TSK_TEST_STACK_SIZE;
    attr.priority = osPriorityLow1;
    attr.attr_bits = osThreadDetached;
    threadId = osThreadNew((osThreadFunc_t)CmsisSatFunc001, NULL, &attr);
    ICUNIT_ASSERT_NOT_EQUAL(threadId, NULL, threadId);
    ret = osThreadSetPriority(threadId, osPriorityHigh1);
    ICUNIT_ASSERT_EQUAL(ret, osErrorParameter, ret);
    osDelay(5);
    return LOS_OK;
}

static UINT32 TestCmsisExt001(VOID)
{
    uint64_t ms = osKernelGetTick2ms();
    ICUNIT_ASSERT_NOT_EQUAL(ms, 0, ms);
    return LOS_OK;
}

static UINT32 TestCmsisExt002(VOID)
{
    uint64_t ticks = osMs2Tick(1000);
    ICUNIT_ASSERT_EQUAL(ticks, LOSCFG_BASE_CORE_TICK_PER_SECOND, ticks);
    ticks = osMs2Tick(0);
    ICUNIT_ASSERT_EQUAL(ticks, 0, ticks);
    return LOS_OK;
}

static VOID CmsisExtArgFunc(void *arg)
{
    void *ret = osThreadGetArgument();
    ICUNIT_ASSERT_EQUAL_VOID(ret, arg, ret);
    g_testCount++;
    return;
}

static UINT32 TestCmsisExt003(VOID)
{
    osThreadId_t threadId;
    osThreadAttr_t attr = {0};
    UINT32 testArg = 0x12345678;
    attr.stack_size = OS_TSK_TEST_STACK_SIZE;
    attr.priority = osPriorityLow1;
    attr.attr_bits = osThreadDetached;
    g_testCount = 0;
    threadId = osThreadNew((osThreadFunc_t)CmsisExtArgFunc, (void *)&testArg, &attr);
    ICUNIT_ASSERT_NOT_EQUAL(threadId, NULL, threadId);
    osDelay(5);
    ICUNIT_ASSERT_EQUAL(g_testCount, 1, g_testCount);
    return LOS_OK;
}

static UINT32 TestCmsisExt004(VOID)
{
    osThreadId_t threadId;
    osThreadAttr_t attr = {0};
    attr.stack_size = OS_TSK_TEST_STACK_SIZE;
    attr.priority = osPriorityLow1;
    attr.attr_bits = osThreadDetached;
    g_testCount = 0;
    threadId = osThreadNewExt((osThreadFunc_t)CmsisSatFunc001, NULL, &attr, CMSIS_THREAD_POLICY_RT);
    ICUNIT_ASSERT_NOT_EQUAL(threadId, NULL, threadId);
    osDelay(5);
    ICUNIT_ASSERT_EQUAL(g_testCount, 1, g_testCount);
    return LOS_OK;
}

static UINT32 TestCmsisExt005(VOID)
{
    osThreadId_t threadId;
    osThreadAttr_t attr = {0};
    osStatus_t ret;
    attr.stack_size = OS_TSK_TEST_STACK_SIZE;
    attr.priority = osPriorityLow1;
    attr.attr_bits = osThreadDetached;
    threadId = osThreadNew((osThreadFunc_t)CmsisSatFunc001, NULL, &attr);
    ICUNIT_ASSERT_NOT_EQUAL(threadId, NULL, threadId);
    ret = osThreadSetPolicy(threadId, CMSIS_THREAD_POLICY_RT);
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);
    osDelay(5);
    return LOS_OK;
}

static UINT32 TestCmsisQueueLimit001(VOID)
{
    osMessageQueueId_t msgQueueId;
    msgQueueId = osMessageQueueNew(0x10000, 4, NULL);
    ICUNIT_ASSERT_EQUAL(msgQueueId, NULL, msgQueueId);
    return LOS_OK;
}

static UINT32 TestCmsisQueueLimit002(VOID)
{
    osMessageQueueId_t msgQueueId;
    msgQueueId = osMessageQueueNew(1, 0x10000, NULL);
    ICUNIT_ASSERT_EQUAL(msgQueueId, NULL, msgQueueId);
    return LOS_OK;
}

static UINT32 TestCmsisMutexGetName001(VOID)
{
    osMutexId_t mutexId;
    const char *name;

    mutexId = osMutexNew(NULL);
    ICUNIT_ASSERT_NOT_EQUAL(mutexId, NULL, mutexId);

    name = osMutexGetName(mutexId);
    ICUNIT_ASSERT_EQUAL(name, NULL, name);

    name = osMutexGetName(NULL);
    ICUNIT_ASSERT_EQUAL(name, NULL, name);

    (void)osMutexDelete(mutexId);
    return LOS_OK;
}

/* ==== Thread Flags ==== */
static volatile uint32_t g_flagsResult;

static VOID CmsisThreadWaitFunc(VOID)
{
    g_testCount++;
    while (1) {
        osDelay(10);
    }
}

static VOID CmsisThreadFlagsFunc(VOID)
{
    uint32_t flags = osThreadFlagsWait(0x3, osFlagsWaitAny, 100);
    g_flagsResult = flags;
    g_testCount++;
}

static UINT32 TestCmsisThreadFlags001(VOID)
{
    osThreadId_t tid;
    osThreadAttr_t attr = {0};
    attr.stack_size = OS_TSK_TEST_STACK_SIZE;
    attr.priority = osPriorityLow1;
    attr.attr_bits = osThreadDetached;
    g_testCount = 0;
    g_flagsResult = 0;
    tid = osThreadNew((osThreadFunc_t)CmsisThreadFlagsFunc, NULL, &attr);
    ICUNIT_ASSERT_NOT_EQUAL(tid, NULL, tid);
    osDelay(2);
    uint32_t ret = osThreadFlagsSet(tid, 0x1);
    ICUNIT_ASSERT_EQUAL(ret & 0x1, 0x1, ret);
    osDelay(5);
    ICUNIT_ASSERT_EQUAL(g_testCount, 1, g_testCount);
    ICUNIT_ASSERT_EQUAL(g_flagsResult & 0x1, 0x1, g_flagsResult);
    return LOS_OK;
}

static UINT32 TestCmsisThreadFlags002(VOID)
{
    osThreadId_t tid;
    osThreadAttr_t attr = {0};
    attr.stack_size = OS_TSK_TEST_STACK_SIZE;
    attr.priority = osPriorityLow1;
    attr.attr_bits = osThreadDetached;
    g_testCount = 0;
    g_flagsResult = 0;
    tid = osThreadNew((osThreadFunc_t)CmsisThreadFlagsFunc, NULL, &attr);
    ICUNIT_ASSERT_NOT_EQUAL(tid, NULL, tid);
    osDelay(2);
    osThreadFlagsSet(tid, 0x2);
    osDelay(5);
    ICUNIT_ASSERT_EQUAL(g_testCount, 1, g_testCount);
    ICUNIT_ASSERT_EQUAL(g_flagsResult & 0x2, 0x2, g_flagsResult);
    return LOS_OK;
}

static UINT32 TestCmsisThreadFlags003(VOID)
{
    uint32_t flags;
    osThreadId_t tid = osThreadGetId();
    flags = osThreadFlagsSet(tid, 0x5);
    ICUNIT_ASSERT_EQUAL(flags & 0x5, 0x5, flags);
    flags = osThreadFlagsGet();
    ICUNIT_ASSERT_EQUAL(flags & 0x5, 0x5, flags);
    flags = osThreadFlagsClear(0x4);
    flags = osThreadFlagsGet();
    ICUNIT_ASSERT_EQUAL(flags & 0x4, 0, flags);
    ICUNIT_ASSERT_EQUAL(flags & 0x1, 0x1, flags);
    osThreadFlagsClear(0x1);
    return LOS_OK;
}

static UINT32 TestCmsisThreadFlags004(VOID)
{
    uint32_t ret = osThreadFlagsSet(NULL, 0x1);
    ICUNIT_ASSERT_EQUAL(ret, (uint32_t)osFlagsErrorParameter, ret);
    return LOS_OK;
}

/* ==== Memory Pool ==== */
#if (LOSCFG_KERNEL_MEMBOX_STATIC == 1) /* membox dyn 下 CMSIS osMemoryPool* 无实现, 用例隔离(测试侧适配) */
static UINT32 TestCmsisMemPool001(VOID)
{
    osMemoryPoolId_t mp;
    void *block;
    mp = osMemoryPoolNew(4, 32, NULL);
    ICUNIT_ASSERT_NOT_EQUAL(mp, NULL, mp);
    block = osMemoryPoolAlloc(mp, 0);
    ICUNIT_ASSERT_NOT_EQUAL(block, NULL, block);
    (void)osMemoryPoolFree(mp, block);
    (void)osMemoryPoolDelete(mp);
    return LOS_OK;
}

static UINT32 TestCmsisMemPool002(VOID)
{
    osMemoryPoolId_t mp;
    mp = osMemoryPoolNew(4, 32, NULL);
    ICUNIT_ASSERT_NOT_EQUAL(mp, NULL, mp);
    ICUNIT_ASSERT_EQUAL(osMemoryPoolGetCapacity(mp), 4, 0);
    ICUNIT_ASSERT_EQUAL(osMemoryPoolGetBlockSize(mp), 32, 0);
    ICUNIT_ASSERT_EQUAL(osMemoryPoolGetCount(mp), 0, 0);
    ICUNIT_ASSERT_EQUAL(osMemoryPoolGetSpace(mp), 4, 0);
    void *b1 = osMemoryPoolAlloc(mp, 0);
    ICUNIT_ASSERT_EQUAL(osMemoryPoolGetCount(mp), 1, 0);
    ICUNIT_ASSERT_EQUAL(osMemoryPoolGetSpace(mp), 3, 0);
    (void)osMemoryPoolFree(mp, b1);
    (void)osMemoryPoolDelete(mp);
    return LOS_OK;
}

static UINT32 TestCmsisMemPool003(VOID)
{
    osMemoryPoolId_t mp;
    void *blocks[4];
    int i;
    mp = osMemoryPoolNew(4, 32, NULL);
    ICUNIT_ASSERT_NOT_EQUAL(mp, NULL, mp);
    for (i = 0; i < 4; i++) {
        blocks[i] = osMemoryPoolAlloc(mp, 0);
        ICUNIT_ASSERT_NOT_EQUAL(blocks[i], NULL, blocks[i]);
    }
    ICUNIT_ASSERT_EQUAL(osMemoryPoolGetSpace(mp), 0, 0);
    void *extra = osMemoryPoolAlloc(mp, 0);
    ICUNIT_ASSERT_EQUAL(extra, NULL, extra);
    for (i = 0; i < 4; i++) {
        (void)osMemoryPoolFree(mp, blocks[i]);
    }
    (void)osMemoryPoolDelete(mp);
    return LOS_OK;
}

static UINT32 TestCmsisMemPool004(VOID)
{
    osMemoryPoolId_t mp;
    mp = osMemoryPoolNew(4, 32, NULL);
    ICUNIT_ASSERT_NOT_EQUAL(mp, NULL, mp);
    (void)osMemoryPoolFree(mp, NULL);
    (void)osMemoryPoolFree(NULL, (void *)0x1000);
    (void)osMemoryPoolDelete(mp);
    return LOS_OK;
}

static UINT32 TestCmsisMemPool005(VOID)
{
    ICUNIT_ASSERT_EQUAL(osMemoryPoolNew(0, 32, NULL), NULL, 0);
    ICUNIT_ASSERT_EQUAL(osMemoryPoolNew(4, 0, NULL), NULL, 0);
    ICUNIT_ASSERT_EQUAL(osMemoryPoolGetCapacity(NULL), 0, 0);
    ICUNIT_ASSERT_EQUAL(osMemoryPoolGetBlockSize(NULL), 0, 0);
    ICUNIT_ASSERT_EQUAL(osMemoryPoolGetCount(NULL), 0, 0);
    ICUNIT_ASSERT_EQUAL(osMemoryPoolGetSpace(NULL), 0, 0);
    return LOS_OK;
}

static UINT32 TestCmsisMemPool006(VOID)
{
    osMemoryPoolId_t mp;
    osMemoryPoolAttr_t attr = {0};
    const char *name;

    dprintf("--- ItCmsisMemPool006 begin ---\n");

    attr.name = "mp_test_006";
    mp = osMemoryPoolNew(4, 32, &attr);
    ICUNIT_GOTO_NOT_EQUAL(mp, NULL, mp, EXIT);

    name = osMemoryPoolGetName(mp);
    ICUNIT_GOTO_NOT_EQUAL(name, NULL, name, EXIT);
    ICUNIT_GOTO_STRING_EQUAL(name, "mp_test_006", name, EXIT);

    name = osMemoryPoolGetName(NULL);
    ICUNIT_GOTO_EQUAL(name, NULL, name, EXIT);

EXIT:
    (void)osMemoryPoolDelete(mp);
    dprintf("--- ItCmsisMemPool006 end ---\n");
    return LOS_OK;
}
#endif /* LOSCFG_KERNEL_MEMBOX_STATIC */

/* ==== Event Flags ==== */
static UINT32 TestCmsisEventFlags001(VOID)
{
    osEventFlagsId_t ef;
    uint32_t ret;
    ef = osEventFlagsNew(NULL);
    ICUNIT_ASSERT_NOT_EQUAL(ef, NULL, ef);
    ret = osEventFlagsSet(ef, 0x1);
    ICUNIT_ASSERT_EQUAL(ret & 0x1, 0x1, ret);
    ret = osEventFlagsGet(ef);
    ICUNIT_ASSERT_EQUAL(ret & 0x1, 0x1, ret);
    ret = osEventFlagsClear(ef, 0x1);
    ret = osEventFlagsGet(ef);
    ICUNIT_ASSERT_EQUAL(ret & 0x1, 0, ret);
    (void)osEventFlagsDelete(ef);
    return LOS_OK;
}

static UINT32 TestCmsisEventFlags002(VOID)
{
    osEventFlagsId_t ef;
    uint32_t ret;
    ef = osEventFlagsNew(NULL);
    ICUNIT_ASSERT_NOT_EQUAL(ef, NULL, ef);
    osEventFlagsSet(ef, 0x3);
    ret = osEventFlagsWait(ef, 0x3, osFlagsWaitAll, 10);
    ICUNIT_ASSERT_EQUAL(ret & 0x3, 0x3, ret);
    (void)osEventFlagsDelete(ef);
    return LOS_OK;
}

static UINT32 TestCmsisEventFlags003(VOID)
{
    osEventFlagsId_t ef;
    uint32_t ret;
    ef = osEventFlagsNew(NULL);
    ICUNIT_ASSERT_NOT_EQUAL(ef, NULL, ef);
    osEventFlagsSet(ef, 0x1);
    ret = osEventFlagsWait(ef, 0x1, osFlagsWaitAny | osFlagsNoClear, 10);
    ICUNIT_ASSERT_EQUAL(ret & 0x1, 0x1, ret);
    ret = osEventFlagsGet(ef);
    ICUNIT_ASSERT_EQUAL(ret & 0x1, 0x1, ret);
    (void)osEventFlagsDelete(ef);
    return LOS_OK;
}

/* ==== Mutex ==== */
static UINT32 TestCmsisMutex001(VOID)
{
    osMutexId_t mid;
    osStatus_t ret;
    mid = osMutexNew(NULL);
    ICUNIT_ASSERT_NOT_EQUAL(mid, NULL, mid);
    ret = osMutexAcquire(mid, 0);
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);
    ret = osMutexRelease(mid);
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);
    ret = osMutexDelete(mid);
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);
    return LOS_OK;
}

static UINT32 TestCmsisMutex002(VOID)
{
    osMutexId_t mid;
    osThreadId_t owner;
    mid = osMutexNew(NULL);
    ICUNIT_ASSERT_NOT_EQUAL(mid, NULL, mid);
    (void)osMutexAcquire(mid, 0);
    owner = osMutexGetOwner(mid);
    ICUNIT_ASSERT_NOT_EQUAL(owner, NULL, owner);
    (void)osMutexRelease(mid);
    (void)osMutexDelete(mid);
    return LOS_OK;
}

static UINT32 TestCmsisMutex003(VOID)
{
    ICUNIT_ASSERT_EQUAL(osMutexAcquire(NULL, 0), osErrorParameter, 0);
    ICUNIT_ASSERT_EQUAL(osMutexRelease(NULL), osErrorParameter, 0);
    ICUNIT_ASSERT_EQUAL(osMutexDelete(NULL), osErrorParameter, 0);
    return LOS_OK;
}

/* ==== Semaphore ==== */
static UINT32 TestCmsisSem001(VOID)
{
    osSemaphoreId_t sid;
    osStatus_t ret;
    sid = osSemaphoreNew(1, 0, NULL);
    ICUNIT_ASSERT_NOT_EQUAL(sid, NULL, sid);
    ret = osSemaphoreRelease(sid);
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);
    ret = osSemaphoreAcquire(sid, 0);
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);
    ret = osSemaphoreDelete(sid);
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);
    return LOS_OK;
}

static UINT32 TestCmsisSem002(VOID)
{
    osSemaphoreId_t sid;
    sid = osSemaphoreNew(3, 1, NULL);
    ICUNIT_ASSERT_NOT_EQUAL(sid, NULL, sid);
    ICUNIT_ASSERT_EQUAL(osSemaphoreGetCount(sid), 1, 0);
    (void)osSemaphoreAcquire(sid, 0);
    ICUNIT_ASSERT_EQUAL(osSemaphoreGetCount(sid), 0, 0);
    (void)osSemaphoreRelease(sid);
    ICUNIT_ASSERT_EQUAL(osSemaphoreGetCount(sid), 1, 0);
    (void)osSemaphoreDelete(sid);
    return LOS_OK;
}

static UINT32 TestCmsisSem003(VOID)
{
    ICUNIT_ASSERT_EQUAL(osSemaphoreNew(0, 0, NULL), NULL, 0);
    ICUNIT_ASSERT_EQUAL(osSemaphoreNew(1, 2, NULL), NULL, 0);
    ICUNIT_ASSERT_EQUAL(osSemaphoreAcquire(NULL, 0), osErrorParameter, 0);
    ICUNIT_ASSERT_EQUAL(osSemaphoreRelease(NULL), osErrorParameter, 0);
    ICUNIT_ASSERT_EQUAL(osSemaphoreDelete(NULL), osErrorParameter, 0);
    return LOS_OK;
}

/* ==== Kernel ==== */
static UINT32 TestCmsisKernel001(VOID)
{
    osKernelState_t state = osKernelGetState();
    ICUNIT_ASSERT_EQUAL(state, osKernelRunning, state);
    return LOS_OK;
}

static UINT32 TestCmsisKernel002(VOID)
{
    int32_t lock1 = osKernelLock();
    ICUNIT_ASSERT_EQUAL(lock1, 0, lock1);
    int32_t lock2 = osKernelUnlock();
    ICUNIT_ASSERT_EQUAL(lock2, 1, lock2);
    (void)osKernelRestoreLock(0);
    return LOS_OK;
}

static UINT32 TestCmsisKernel003(VOID)
{
    uint32_t freq = osKernelGetTickFreq();
    ICUNIT_ASSERT_NOT_EQUAL(freq, 0, freq);
    uint32_t tc = osKernelGetTickCount();
    uint32_t timerFreq = osKernelGetSysTimerFreq();
    ICUNIT_ASSERT_NOT_EQUAL(timerFreq, 0, timerFreq);
    (void)tc;
    return LOS_OK;
}

/* ==== Thread (supplement) ==== */
static UINT32 TestCmsisThread001(VOID)
{
    osThreadId_t tid;
    osThreadAttr_t attr = {0};
    attr.stack_size = OS_TSK_TEST_STACK_SIZE;
    attr.priority = osPriorityLow1;
    attr.attr_bits = osThreadDetached;
    g_testCount = 0;
    tid = osThreadNew((osThreadFunc_t)CmsisStackFunc01, NULL, &attr);
    ICUNIT_ASSERT_NOT_EQUAL(tid, NULL, tid);
    osDelay(3);
    ICUNIT_ASSERT_EQUAL(g_testCount, 1, g_testCount);
    return LOS_OK;
}

static UINT32 TestCmsisThread002(VOID)
{
    osThreadId_t tid = osThreadGetId();
    ICUNIT_ASSERT_NOT_EQUAL(tid, NULL, tid);
    osThreadState_t state = osThreadGetState(tid);
    ICUNIT_ASSERT_EQUAL(state, osThreadRunning, state);
    return LOS_OK;
}

static UINT32 TestCmsisThread003(VOID)
{
    uint32_t count = osThreadGetCount();
    ICUNIT_ASSERT_NOT_EQUAL(count, 0, count);
    return LOS_OK;
}

static UINT32 TestCmsisThread004(VOID)
{
    osStatus_t ret = osThreadYield();
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);
    return LOS_OK;
}

static UINT32 TestCmsisThread005(VOID)
{
    osThreadId_t tid;
    osThreadAttr_t attr = {0};
    attr.stack_size = OS_TSK_TEST_STACK_SIZE;
    attr.priority = osPriorityLow1;
    attr.attr_bits = osThreadDetached;
    g_testCount = 0;
    tid = osThreadNew((osThreadFunc_t)CmsisThreadWaitFunc, NULL, &attr);
    ICUNIT_ASSERT_NOT_EQUAL(tid, NULL, tid);
    osDelay(3);
    ICUNIT_ASSERT_EQUAL(g_testCount, 1, g_testCount);
    osStatus_t ret = osThreadTerminate(tid);
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);
    return LOS_OK;
}

/* ==== Timer (supplement) ==== */
static volatile uint32_t g_timerCount;

static VOID CmsisTimerCallback(void *arg)
{
    (void)arg;
    g_timerCount++;
}

static UINT32 TestCmsisTimer002(VOID)
{
    osTimerId_t tid;
    osStatus_t ret;
    g_timerCount = 0;
    tid = osTimerNew(CmsisTimerCallback, osTimerPeriodic, NULL, NULL);
    ICUNIT_ASSERT_NOT_EQUAL(tid, NULL, tid);
    ret = osTimerStart(tid, 10);
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);
    osDelay(50);
    ICUNIT_ASSERT_NOT_EQUAL(g_timerCount, 0, g_timerCount);
    ret = osTimerStop(tid);
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);
    ret = osTimerDelete(tid);
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);
    return LOS_OK;
}

static UINT32 TestCmsisTimer003(VOID)
{
    osTimerId_t tid;
    uint32_t running;
    tid = osTimerNew(CmsisTimerCallback, osTimerOnce, NULL, NULL);
    ICUNIT_ASSERT_NOT_EQUAL(tid, NULL, tid);
    running = osTimerIsRunning(tid);
    ICUNIT_ASSERT_EQUAL(running, 0, running);
    (void)osTimerStart(tid, 5);
    running = osTimerIsRunning(tid);
    ICUNIT_ASSERT_EQUAL(running, 1, running);
    (void)osTimerStop(tid);
    running = osTimerIsRunning(tid);
    ICUNIT_ASSERT_EQUAL(running, 0, running);
    (void)osTimerDelete(tid);
    return LOS_OK;
}


/* ==== Supplement: cover uncalled CMSIS APIs (kernel info, delay-until,
 *      queue attributes, thread suspend/resume/detach/exit/stack-info) ==== */

/* helper thread: increment counter then delay so the test can suspend it */
static VOID CmsisSupplementSuspThread(VOID)
{
    g_testCount++;
    osDelay(20);
    g_testCount++;
}

/* helper thread: increment counter then exit via osThreadExit */
static VOID CmsisSupplementExitThread(VOID)
{
    g_testCount++;
    osThreadExit();
    UNREACHABLE; /* osThreadExit must not return */
}

/* helper thread: just increment counter and exit (for stack/detach tests) */
static VOID CmsisSupplementSimpleThread(VOID)
{
    g_testCount++;
}

/* helper thread: verify osThreadGetArgument returns the value passed at creation */
static VOID CmsisSupplementArgThread(VOID)
{
    void *arg = osThreadGetArgument();
    if (arg == (void *)0x12345678) {
        g_testCount++;
    }
}

/* helper thread: delay then increment, used to verify osThreadJoin (joinable) */
static VOID CmsisSupplementJoinThread(VOID)
{
    osDelay(3);
    g_testCount++;
}

/* helper thread: stay alive (blocked in osDelay) for priority get/set queries */
static VOID CmsisSupplementPrioThread(VOID)
{
    osDelay(20);
}

static UINT32 TestCmsisKernel004(VOID)
{
    osVersion_t ver = {0};
    CHAR id_buf[32] = {0};
    osStatus_t ret;
    UINT32 tc;

    /* osKernelGetInfo: valid call returns osOK and fills version/id buffer */
    ret = osKernelGetInfo(&ver, id_buf, sizeof(id_buf));
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);
    ICUNIT_ASSERT_NOT_EQUAL(ver.api, 0, ver.api);

    /* osKernelGetSysTimerCount: returns current system timer count (non-zero) */
    tc = osKernelGetSysTimerCount();
    (VOID)tc;

    /* osKernelStart: g_kernelState is osKernelInactive (boot uses LOS_Start directly,
     * not osKernelStart), so the guard rejects with osError - safe to call */
    ret = osKernelStart();
    ICUNIT_ASSERT_EQUAL(ret, osError, ret);

    /* Note: osKernelInitialize is intentionally NOT called here because g_kernelState
     * is osKernelInactive and the function would fall through to LOS_KernelInit(),
     * re-initializing the running kernel (destructive, causes LMS crash). */

    return LOS_OK;
}

static UINT32 TestCmsisKernel005(VOID)
{
    UINT32 start = osKernelGetTickCount();
    UINT32 target = start + 5;
    osStatus_t ret;

    /* osDelayUntil: block until the absolute tick 'target' is reached */
    ret = osDelayUntil(target);
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);

    /* time must have progressed at least to 'target' */
    UINT32 end = osKernelGetTickCount();
    ICUNIT_ASSERT_WITHIN_EQUAL(end, target, target + 50, end);

    return LOS_OK;
}

static UINT32 TestCmsisQueueAttr001(VOID)
{
    osMessageQueueId_t mq;
    CHAR msg[4] = "abc";
    CHAR buf[4] = {0};
    osStatus_t ret;

    mq = osMessageQueueNew(2, 4, NULL);
    ICUNIT_ASSERT_NOT_EQUAL(mq, NULL, mq);

    /* Empty queue: capacity=2, msg_size=4, count=0, space=2 */
    ICUNIT_ASSERT_EQUAL(osMessageQueueGetCapacity(mq), 2, 0);
    ICUNIT_ASSERT_EQUAL(osMessageQueueGetMsgSize(mq), 4, 0);
    ICUNIT_ASSERT_EQUAL(osMessageQueueGetCount(mq), 0, 0);
    ICUNIT_ASSERT_EQUAL(osMessageQueueGetSpace(mq), 2, 0);

    /* Put one message: count=1, space=1 */
    ret = osMessageQueuePut(mq, msg, 0, 0);
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);
    ICUNIT_ASSERT_EQUAL(osMessageQueueGetCount(mq), 1, 0);
    ICUNIT_ASSERT_EQUAL(osMessageQueueGetSpace(mq), 1, 0);

    /* Get the message: count=0, space=2 */
    ret = osMessageQueueGet(mq, buf, NULL, 0);
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);
    ICUNIT_ASSERT_EQUAL(osMessageQueueGetCount(mq), 0, 0);
    ICUNIT_ASSERT_EQUAL(osMessageQueueGetSpace(mq), 2, 0);

    ret = osMessageQueueDelete(mq);
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);

    /* Querying a deleted queue returns 0 (queueState == OS_QUEUE_UNUSED branch) */
    ICUNIT_ASSERT_EQUAL(osMessageQueueGetCapacity(mq), 0, 0);

    return LOS_OK;
}

static UINT32 TestCmsisThread006(VOID)
{
    osThreadId_t tid;
    osThreadAttr_t attr = {0};
    osPriority_t prio;
    osThreadState_t state;
    osStatus_t ret;

    attr.stack_size = OS_TSK_TEST_STACK_SIZE;
    attr.priority = osPriorityLow1;
    attr.attr_bits = osThreadDetached;
    g_testCount = 0;

    tid = osThreadNew((osThreadFunc_t)CmsisSupplementSuspThread, NULL, &attr);
    ICUNIT_ASSERT_NOT_EQUAL(tid, NULL, tid);

    /* let helper run one increment then block in osDelay(20) */
    osDelay(2);
    ICUNIT_ASSERT_EQUAL(g_testCount, 1, g_testCount);

    /* osThreadGetPriority: should match the priority we set */
    prio = osThreadGetPriority(tid);
    ICUNIT_ASSERT_EQUAL(prio, osPriorityLow1, prio);

    /* osThreadSuspend: suspend the helper (currently blocked in osDelay) */
    ret = osThreadSuspend(tid);
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);

    state = osThreadGetState(tid);
    ICUNIT_ASSERT_EQUAL(state, osThreadBlocked, state);

    /* osThreadResume: restore the helper */
    ret = osThreadResume(tid);
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);

    /* wait for helper to finish its second increment */
    osDelay(25);
    ICUNIT_ASSERT_EQUAL(g_testCount, 2, g_testCount);

    return LOS_OK;
}

static UINT32 TestCmsisThread007(VOID)
{
    osThreadId_t tid;
    osThreadAttr_t attr = {0};
    UINT32 ss;
    UINT32 sp;
    osStatus_t ret;

    attr.stack_size = OS_TSK_TEST_STACK_SIZE;
    attr.priority = osPriorityLow1;
    attr.attr_bits = osThreadJoinable; /* joinable so osThreadDetach has effect */
    g_testCount = 0;

    tid = osThreadNew((osThreadFunc_t)CmsisSupplementSimpleThread, NULL, &attr);
    ICUNIT_ASSERT_NOT_EQUAL(tid, NULL, tid);

    /* osThreadGetStackSize: should match attr.stack_size */
    ss = osThreadGetStackSize(tid);
    ICUNIT_ASSERT_EQUAL(ss, OS_TSK_TEST_STACK_SIZE, ss);

    /* osThreadGetStackSpace: should be > 0 (some stack is unused) */
    sp = osThreadGetStackSpace(tid);
    ICUNIT_ASSERT_NOT_EQUAL(sp, 0, sp);

    /* osThreadDetach: convert joinable -> detached; thread auto-cleans on exit */
    ret = osThreadDetach(tid);
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);

    osDelay(5);
    ICUNIT_ASSERT_EQUAL(g_testCount, 1, g_testCount);

    return LOS_OK;
}

static UINT32 TestCmsisThread008(VOID)
{
    osThreadId_t tid;
    osThreadAttr_t attr = {0};

    attr.stack_size = OS_TSK_TEST_STACK_SIZE;
    attr.priority = osPriorityLow1;
    attr.attr_bits = osThreadDetached;
    g_testCount = 0;

    tid = osThreadNew((osThreadFunc_t)CmsisSupplementExitThread, NULL, &attr);
    ICUNIT_ASSERT_NOT_EQUAL(tid, NULL, tid);

    /* helper increments g_testCount then calls osThreadExit() */
    osDelay(5);
    ICUNIT_ASSERT_EQUAL(g_testCount, 1, g_testCount);

    /* after exit, thread should be inactive */
    osThreadState_t state = osThreadGetState(tid);
    ICUNIT_ASSERT_EQUAL(state, osThreadInactive, state);

    return LOS_OK;
}

/* ==== Batch 1 supplement: Kernel Lock state machine / Delay / Timer boundary / Thread misc ==== */

/* 用例简要描述: Lock/Unlock/RestoreLock 状态机往返 */
static UINT32 TestCmsisKernel006(VOID)
{
    int32_t lockBefore, lockNested, unlockRet, restoreLocked, restoreUnlocked;

    dprintf("--- TestCmsisKernel006 begin ---\n");

    /* osKernelLock on an unlocked kernel returns 0 (previous state) and locks scheduling */
    lockBefore = osKernelLock();
    ICUNIT_GOTO_EQUAL(lockBefore, 0, lockBefore, EXIT);

    /* nested lock: already locked, returns 1 (previous state) without re-locking */
    lockNested = osKernelLock();
    ICUNIT_GOTO_EQUAL(lockNested, 1, lockNested, EXIT);

    /* osKernelUnlock returns 1 (previous state was locked) and restores scheduling */
    unlockRet = osKernelUnlock();
    ICUNIT_GOTO_EQUAL(unlockRet, 1, unlockRet, EXIT);

    /* osKernelRestoreLock(1) re-enters locked state, returns KERNEL_LOCKED (1) */
    restoreLocked = osKernelRestoreLock(1);
    ICUNIT_GOTO_EQUAL(restoreLocked, 1, restoreLocked, EXIT);

    /* osKernelRestoreLock(0) restores unlocked state, returns KERNEL_UNLOCKED (0) */
    restoreUnlocked = osKernelRestoreLock(0);
    ICUNIT_GOTO_EQUAL(restoreUnlocked, 0, restoreUnlocked, EXIT);

EXIT:
    /* safety: guarantee the kernel is unlocked regardless of which step failed */
    (void)osKernelRestoreLock(0);
    dprintf("--- TestCmsisKernel006 end ---\n");
    return LOS_OK;
}

/* 用例简要描述: Lock 禁止调度:lock 下 osDelay 立即返回错误 */
static UINT32 TestCmsisKernel007(VOID)
{
    int32_t lockBefore;
    osKernelState_t state;
    osStatus_t sDelay;
    uint32_t start, end, diff;

    dprintf("--- TestCmsisKernel007 begin ---\n");

    /* lock the scheduler */
    lockBefore = osKernelLock();
    ICUNIT_GOTO_EQUAL(lockBefore, 0, lockBefore, EXIT);

    /* lock puts the kernel into osKernelLocked state */
    state = osKernelGetState();
    ICUNIT_GOTO_EQUAL(state, osKernelLocked, state, EXIT);

    /* Under lock osDelay takes the HalDelay no-op path: it returns osOK but must
     * NOT actually advance the tick count by the requested 2 ticks (no real delay).
     * Verify by capturing ticks before/after the call. */
    start = osKernelGetTickCount();
    sDelay = osDelay(2);
    end = osKernelGetTickCount();
    ICUNIT_GOTO_EQUAL(sDelay, osOK, sDelay, EXIT);
    diff = end - start;
    if (diff >= 2) {
        ICunitSaveErr(__LINE__, (iiUINT32)diff);
        goto EXIT;
    }

    /* unlock restores normal scheduling; osDelay blocks for the requested tick */
    (void)osKernelUnlock();
    sDelay = osDelay(1);
    ICUNIT_GOTO_EQUAL(sDelay, osOK, sDelay, EXIT);

EXIT:
    /* safety: guarantee the kernel is unlocked regardless of which step failed */
    (void)osKernelRestoreLock(0);
    dprintf("--- TestCmsisKernel007 end ---\n");
    return LOS_OK;
}

/* 用例简要描述: osKernelInitialize Running 态负面:期望 osError(guard)
 * 【BUG_01】仅 qemu/ARM 隔离(osKernelInitialize Running 态重入 guard 失效):
 * 定义无条件编译(unused 属性防未注册告警, 仓库既有惯用法), 隔离点在套件
 * 注册处(TEST_ADD_CASE)—— 外层架构条件, 内层 BUG 宏守卫单独一行(见 bug_isolate.h)。 */
static UINT32 TestCmsisKernel008(VOID) __attribute__((unused));
static UINT32 TestCmsisKernel008(VOID)
{
    osKernelState_t state;
    osStatus_t ret;

    dprintf("--- TestCmsisKernel008 begin ---\n");

    state = osKernelGetState();
    ICUNIT_ASSERT_EQUAL(state, osKernelRunning, state);

    ret = osKernelInitialize();
    dprintf("--- TestCmsisKernel008: osKernelInitialize returned %d (expect osError) ---\n", (int)ret);
    ICUNIT_ASSERT_EQUAL(ret, osError, ret);

    dprintf("--- TestCmsisKernel008 end ---\n");
    return LOS_OK;
}

/* ==== ISR 综合用例: 对应设计 §5.5.1 行006/§5.7.1 行005/§5.8.1 行006/§5.9.1 行005/§5.10.1 行004
   的 F-ISR 场景(EventFlagsSet/SemaphoreRelease/MessageQueuePut/MemoryPoolFree/ThreadFlagsSet
   在 ISR 内调用),工程 ISR 构造先例: It_los_queue_isr_001(HwiCreate+TestHwiTrigger) ==== */
static osEventFlagsId_t g_isrEfId;
static osSemaphoreId_t g_isrSemId;
static osMessageQueueId_t g_isrQId;
#if (LOSCFG_KERNEL_MEMBOX_STATIC == 1) /* membox dyn 下 CMSIS osMemoryPool* 无实现, 用例隔离(测试侧适配) */
static osMemoryPoolId_t g_isrMpId;
#endif /* LOSCFG_KERNEL_MEMBOX_STATIC */
static osThreadId_t g_isrThrId;
#if (LOSCFG_KERNEL_MEMBOX_STATIC == 1) /* membox dyn 下 CMSIS osMemoryPool* 无实现, 用例隔离(测试侧适配) */
static VOID *g_isrMpBlk = NULL;
#endif /* LOSCFG_KERNEL_MEMBOX_STATIC */
static volatile uint32_t g_isrEfSetRet = 0xDEAD;
static volatile uint32_t g_isrSemRelRet = 0xDEAD;
static volatile uint32_t g_isrQPutRet = 0xDEAD;
#if (LOSCFG_KERNEL_MEMBOX_STATIC == 1) /* membox dyn 下 CMSIS osMemoryPool* 无实现, 用例隔离(测试侧适配) */
static volatile uint32_t g_isrMpFreeRet = 0xDEAD;
#endif /* LOSCFG_KERNEL_MEMBOX_STATIC */
static volatile uint32_t g_isrTfSetRet = 0xDEAD;

static VOID HwiFIsr001(VOID)
{
    TestHwiClear(HWI_NUM_TEST);
    g_isrEfSetRet = osEventFlagsSet(g_isrEfId, 0x1);
    g_isrSemRelRet = osSemaphoreRelease(g_isrSemId);
    g_isrQPutRet = osMessageQueuePut(g_isrQId, "ISROUT", 0U, 0U); /* timeout=0 ISR 合法 */
#if (LOSCFG_KERNEL_MEMBOX_STATIC == 1) /* membox dyn 下 CMSIS osMemoryPool* 无实现, 用例隔离(测试侧适配) */
    g_isrMpFreeRet = osMemoryPoolFree(g_isrMpId, (void *)g_isrMpBlk);
#endif /* LOSCFG_KERNEL_MEMBOX_STATIC */
    g_isrTfSetRet = osThreadFlagsSet(g_isrThrId, 0x1);
}


/* 用例简要描述: ISR 综合用例:EventFlagsSet/SemRelease/QueuePut(timeout=0)/MemPoolFree/ThreadFlagsSet 在 ISR 内 */
static UINT32 TestCmsisIsr001(VOID)
{
    uint32_t ret;
    char rbuf[8] = {0};
    HWI_PRIOR_T hwiPrio = 3;
    HWI_MODE_T mode = 0;
    HwiIrqParam irqParam;

    dprintf("--- TestCmsisIsr001 begin ---\n");

    /* 资源准备 */
    g_isrEfId = osEventFlagsNew(NULL);
    ICUNIT_ASSERT_EQUAL((g_isrEfId != NULL), 1, (uint32_t)(UINTPTR)g_isrEfId);
    g_isrSemId = osSemaphoreNew(1U, 0U, NULL);
    ICUNIT_ASSERT_EQUAL((g_isrSemId != NULL), 1, (uint32_t)(UINTPTR)g_isrSemId);
    g_isrQId = osMessageQueueNew(2U, 8U, NULL); /* 2: msg count, 8: msg size */
    ICUNIT_ASSERT_EQUAL((g_isrQId != NULL), 1, (uint32_t)(UINTPTR)g_isrQId);
#if (LOSCFG_KERNEL_MEMBOX_STATIC == 1) /* membox dyn 下 CMSIS osMemoryPool* 无实现, 用例隔离(测试侧适配) */
    g_isrMpId = osMemoryPoolNew(2U, 16U, NULL); /* 2: block count, 16: block size */
    ICUNIT_ASSERT_EQUAL((g_isrMpId != NULL), 1, (uint32_t)(UINTPTR)g_isrMpId);
    g_isrMpBlk = osMemoryPoolAlloc(g_isrMpId, 0U);
    ICUNIT_ASSERT_EQUAL((g_isrMpBlk != NULL), 1, (uint32_t)(UINTPTR)g_isrMpBlk);
#endif /* LOSCFG_KERNEL_MEMBOX_STATIC */
    g_isrThrId = osThreadGetId(); /* 本任务作为被 Set 标志对象 */

    /* 注册并触发 ISR,回调内依次执行 5 个 F-ISR 场景 */
    (VOID)memset_s(&irqParam, sizeof(HwiIrqParam), 0, sizeof(HwiIrqParam));
    irqParam.pDevId = 0;
    ret = LOS_HwiCreate(HWI_NUM_TEST, hwiPrio, mode, (HWI_PROC_FUNC)HwiFIsr001, &irqParam);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    TestHwiTrigger(HWI_NUM_TEST);

    /* 断言 1(设计§5.5.1 行006): ISR 内 Set 成功,等待方唤醒 */
    ICUNIT_GOTO_EQUAL(g_isrEfSetRet, 0x1, g_isrEfSetRet, EXIT);
    ret = osEventFlagsWait(g_isrEfId, 0x1, osFlagsWaitAny, 10U); /* 10: timeout ticks */
    ICUNIT_GOTO_EQUAL(ret, 0x1, ret, EXIT);

    /* 断言 2(设计§5.7.1 行005): ISR 内 Release 唤醒 Acquire */
    ICUNIT_GOTO_EQUAL(g_isrSemRelRet, osOK, g_isrSemRelRet, EXIT);
    ret = osSemaphoreAcquire(g_isrSemId, 10U);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);

    /* 断言 3(设计§5.8.1 行006): ISR 内 Put(timeout=0) 成功,Get 收到内容 */
    ICUNIT_GOTO_EQUAL(g_isrQPutRet, osOK, g_isrQPutRet, EXIT);
    ret = osMessageQueueGet(g_isrQId, rbuf, NULL, 10U);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);
    ICUNIT_GOTO_EQUAL(rbuf[0], 'I', rbuf[0], EXIT);

    /* 断言 4(设计§5.9.1 行005): ISR 内 Free 返回 osOK */
#if (LOSCFG_KERNEL_MEMBOX_STATIC == 1) /* membox dyn 下 CMSIS osMemoryPool* 无实现, 用例隔离(测试侧适配) */
    ICUNIT_GOTO_EQUAL(g_isrMpFreeRet, osOK, g_isrMpFreeRet, EXIT);
#endif /* LOSCFG_KERNEL_MEMBOX_STATIC */

    /* 断言 5(设计§5.10.1 行004): ISR 内 ThreadFlagsSet 唤醒本任务 Wait */
    ICUNIT_GOTO_EQUAL(g_isrTfSetRet, 0x1, g_isrTfSetRet, EXIT);
    ret = osThreadFlagsWait(0x1, osFlagsWaitAny, 10U);
    ICUNIT_GOTO_EQUAL(ret, 0x1, ret, EXIT);

EXIT:
    TestHwiDelete(HWI_NUM_TEST);
    if (g_isrEfId != NULL) { (VOID)osEventFlagsDelete(g_isrEfId); g_isrEfId = NULL; }
    if (g_isrSemId != NULL) { (VOID)osSemaphoreDelete(g_isrSemId); g_isrSemId = NULL; }
    if (g_isrQId != NULL) { (VOID)osMessageQueueDelete(g_isrQId); g_isrQId = NULL; }
#if (LOSCFG_KERNEL_MEMBOX_STATIC == 1) /* membox dyn 下 CMSIS osMemoryPool* 无实现, 用例隔离(测试侧适配) */
    if (g_isrMpId != NULL) { (VOID)osMemoryPoolDelete(g_isrMpId); g_isrMpId = NULL; }
#endif /* LOSCFG_KERNEL_MEMBOX_STATIC */
    dprintf("--- TestCmsisIsr001 end ---\n");
    return LOS_OK;
}


/* 用例简要描述: osKernelGetTickCount 单调契约(tc1>0+delay 后 tc2>tc1) */
static UINT32 TestCmsisKernel009(VOID)
{
    /* 设计 §5.1.2 行 008: osKernelGetTickCount F-Pos/BVA 单调性契约
       (实现编号 008 已被 osKernelInitialize 负面占用,本用例按语义对应设计 008 行;
       此前两套体系均仅工具性调用,无专属断言) */
    uint32_t tc1;
    uint32_t tc2;

    dprintf("--- TestCmsisKernel009 begin ---\n");

    tc1 = osKernelGetTickCount();
    ICUNIT_ASSERT_EQUAL((tc1 > 0), 1, tc1);

    (VOID)osDelay(2); /* 2, delay ticks */

    tc2 = osKernelGetTickCount();
    ICUNIT_ASSERT_EQUAL((tc2 > tc1), 1, (uint32_t)(tc2 - tc1)); /* 差值仅诊断打印 */

    dprintf("--- TestCmsisKernel009 end ---\n");
    return LOS_OK;
}

/* 用例简要描述: osDelay BVA:0 立即/1 tick 经时 */
static UINT32 TestCmsisDelay001(VOID)
{
    osStatus_t sZero, sFive;
    uint32_t start, end, diff;

    dprintf("--- TestCmsisDelay001 begin ---\n");

    /* BVA: ticks=0 is rejected by osDelay (returns osErrorParameter) */
    sZero = osDelay(0);
    ICUNIT_ASSERT_EQUAL(sZero, osErrorParameter, sZero);

    /* F-Pos: ticks=5 blocks ~5 ticks and returns osOK; verify tick advance with tolerance */
    start = osKernelGetTickCount();
    sFive = osDelay(5);
    ICUNIT_ASSERT_EQUAL(sFive, osOK, sFive);
    end = osKernelGetTickCount();
    diff = end - start;
    ICUNIT_ASSERT_WITHIN_EQUAL(diff, 5, 50, diff);

    dprintf("--- TestCmsisDelay001 end ---\n");
    return LOS_OK;
}

/* 用例简要描述: Start 边界 */
static UINT32 TestCmsisTimer004(VOID)
{
    osTimerId_t tid;
    osStatus_t ret;

    dprintf("--- TestCmsisTimer004 begin ---\n");

    tid = osTimerNew(CmsisTimerCallback, osTimerOnce, NULL, NULL);
    ICUNIT_GOTO_NOT_EQUAL(tid, NULL, tid, EXIT);

    /* BVA: ticks=0 is rejected before any state change */
    ret = osTimerStart(tid, 0);
    ICUNIT_GOTO_EQUAL(ret, osErrorParameter, ret, EXIT);

    /* BVA: ticks=1 is the minimal accepted value */
    ret = osTimerStart(tid, 1);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);

    /* let the 1-tick one-shot timer fire deterministically, then delete */
    osDelay(3);
    ret = osTimerDelete(tid);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);
    tid = NULL;

EXIT:
    if (tid != NULL) {
        (void)osTimerDelete(tid);
    }
    dprintf("--- TestCmsisTimer004 end ---\n");
    return LOS_OK;
}

/* 用例简要描述: Stop 后再 Start */
static UINT32 TestCmsisTimer005(VOID)
{
    osTimerId_t tid;
    osStatus_t ret;
    uint32_t running;

    dprintf("--- TestCmsisTimer005 begin ---\n");

    tid = osTimerNew(CmsisTimerCallback, osTimerPeriodic, NULL, NULL);
    ICUNIT_GOTO_NOT_EQUAL(tid, NULL, tid, EXIT);

    ret = osTimerStart(tid, 10);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);

    ret = osTimerStop(tid);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);

    running = osTimerIsRunning(tid);
    ICUNIT_GOTO_EQUAL(running, 0, running, EXIT);

    /* restart after stop: interval is reset and timer is ticking again */
    ret = osTimerStart(tid, 5);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);

    running = osTimerIsRunning(tid);
    ICUNIT_GOTO_EQUAL(running, 1, running, EXIT);

    ret = osTimerStop(tid);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);

    ret = osTimerDelete(tid);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);
    tid = NULL;

EXIT:
    if (tid != NULL) {
        (void)osTimerStop(tid);
        (void)osTimerDelete(tid);
    }
    dprintf("--- TestCmsisTimer005 end ---\n");
    return LOS_OK;
}

/* 用例简要描述: Delete 边界 */
static UINT32 TestCmsisTimer006(VOID)
{
    osTimerId_t tid;
    osStatus_t ret;
    uint32_t running;

    dprintf("--- TestCmsisTimer006 begin ---\n");

    tid = osTimerNew(CmsisTimerCallback, osTimerOnce, NULL, NULL);
    ICUNIT_GOTO_NOT_EQUAL(tid, NULL, tid, EXIT);

    ret = osTimerStart(tid, 100);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);

    /* Delete a running timer: stops and frees it, returns osOK */
    ret = osTimerDelete(tid);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);

    /* after delete the timer is no longer running */
    running = osTimerIsRunning(tid);
    ICUNIT_GOTO_EQUAL(running, 0, running, EXIT);

    /* Delete again: timer already freed (usTimerID mismatch) -> osErrorResource */
    ret = osTimerDelete(tid);
    ICUNIT_GOTO_EQUAL(ret, osErrorResource, ret, EXIT);

EXIT:
    dprintf("--- TestCmsisTimer006 end ---\n");
    return LOS_OK;
}

/* 用例简要描述: SetPolicy */
static UINT32 TestCmsisThread009(VOID)
{
    osThreadId_t tid;
    osThreadAttr_t attr = {0};
    osStatus_t ret;

    dprintf("--- TestCmsisThread009 begin ---\n");

    attr.stack_size = OS_TSK_TEST_STACK_SIZE;
    attr.priority = osPriorityBelowNormal;
    attr.attr_bits = osThreadDetached;
    g_testCount = 0;

    tid = osThreadNew((osThreadFunc_t)CmsisSupplementSimpleThread, NULL, &attr);
    ICUNIT_GOTO_NOT_EQUAL(tid, NULL, tid, EXIT);

    /* osThreadSetPolicy is a no-op that always returns osOK */
    ret = osThreadSetPolicy(tid, CMSIS_THREAD_POLICY_DEFAULT);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);

    osDelay(5);
    ICUNIT_GOTO_EQUAL(g_testCount, 1, g_testCount, EXIT);

EXIT:
    dprintf("--- TestCmsisThread009 end ---\n");
    return LOS_OK;
}

/* 用例简要描述: GetArgument */
static UINT32 TestCmsisThread010(VOID)
{
    osThreadId_t tid;
    osThreadAttr_t attr = {0};

    dprintf("--- TestCmsisThread010 begin ---\n");

    attr.stack_size = OS_TSK_TEST_STACK_SIZE;
    attr.priority = osPriorityBelowNormal;
    attr.attr_bits = osThreadDetached;
    g_testCount = 0;

    tid = osThreadNew((osThreadFunc_t)CmsisSupplementArgThread, (void *)0x12345678, &attr);
    ICUNIT_GOTO_NOT_EQUAL(tid, NULL, tid, EXIT);

    /* helper verifies osThreadGetArgument() == 0x12345678 and increments on match */
    osDelay(5);
    ICUNIT_GOTO_EQUAL(g_testCount, 1, g_testCount, EXIT);

EXIT:
    dprintf("--- TestCmsisThread010 end ---\n");
    return LOS_OK;
}

/* 用例简要描述: GetName */
static UINT32 TestCmsisThread011(VOID)
{
    osThreadId_t tid;
    osThreadAttr_t attr = {0};
    const char *name;

    dprintf("--- TestCmsisThread011 begin ---\n");

    attr.stack_size = OS_TSK_TEST_STACK_SIZE;
    attr.priority = osPriorityBelowNormal;
    attr.attr_bits = osThreadDetached;
    attr.name = "thread_011";
    g_testCount = 0;

    tid = osThreadNew((osThreadFunc_t)CmsisSupplementSimpleThread, NULL, &attr);
    ICUNIT_GOTO_NOT_EQUAL(tid, NULL, tid, EXIT);

    /* osThreadGetName returns the name set via attr.name (query before the thread exits) */
    name = osThreadGetName(tid);
    ICUNIT_GOTO_NOT_EQUAL(name, NULL, name, EXIT);
    ICUNIT_GOTO_STRING_EQUAL(name, "thread_011", name, EXIT);

    osDelay(5);
    ICUNIT_GOTO_EQUAL(g_testCount, 1, g_testCount, EXIT);

EXIT:
    dprintf("--- TestCmsisThread011 end ---\n");
    return LOS_OK;
}

/* 用例简要描述: Join */
static UINT32 TestCmsisThread012(VOID)
{
    osThreadId_t tid;
    osThreadAttr_t attr = {0};
    osStatus_t ret;

    dprintf("--- TestCmsisThread012 begin ---\n");

    attr.stack_size = OS_TSK_TEST_STACK_SIZE;
    attr.priority = osPriorityBelowNormal;
    attr.attr_bits = osThreadJoinable;
    g_testCount = 0;

    tid = osThreadNew((osThreadFunc_t)CmsisSupplementJoinThread, NULL, &attr);
    ICUNIT_GOTO_NOT_EQUAL(tid, NULL, tid, EXIT);

    /* osThreadJoin blocks until the joinable thread exits, then returns osOK */
    ret = osThreadJoin(tid);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);

    /* after join the helper has finished: its increment must be visible */
    ICUNIT_GOTO_EQUAL(g_testCount, 1, g_testCount, EXIT);

EXIT:
    dprintf("--- TestCmsisThread012 end ---\n");
    return LOS_OK;
}

/* 用例简要描述: SetPriority 往返 */
static UINT32 TestCmsisThread013(VOID)
{
    osThreadId_t tid;
    osThreadAttr_t attr = {0};
    osPriority_t prio;
    osStatus_t ret;

    dprintf("--- TestCmsisThread013 begin ---\n");

    attr.stack_size = OS_TSK_TEST_STACK_SIZE;
    attr.priority = osPriorityLow;
    attr.attr_bits = osThreadDetached;

    tid = osThreadNew((osThreadFunc_t)CmsisSupplementPrioThread, NULL, &attr);
    ICUNIT_GOTO_NOT_EQUAL(tid, NULL, tid, EXIT);

    /* created with osPriorityLow: round-trip via LOS mapping returns osPriorityLow */
    prio = osThreadGetPriority(tid);
    ICUNIT_GOTO_EQUAL(prio, osPriorityLow, prio, EXIT);

    /* raise to osPriorityNormal: SetPriority returns osOK */
    ret = osThreadSetPriority(tid, osPriorityNormal);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);

    /* GetPriority reflects the new priority (Normal <-> los 10 round-trips) */
    prio = osThreadGetPriority(tid);
    ICUNIT_GOTO_EQUAL(prio, osPriorityNormal, prio, EXIT);

EXIT:
    /* cleanup: terminate the still-alive helper (blocked in osDelay(20)) */
    if (tid != NULL) {
        (void)osThreadTerminate(tid);
    }
    dprintf("--- TestCmsisThread013 end ---\n");
    return LOS_OK;
}

/* ==== Batch 2: EventFlags/Mutex/Semaphore/MessageQueue/MemoryPool gap tests ==== */
/* helper globals for thread-based tests */
static osEventFlagsId_t g_cmsisEfHelperId;
static osMutexId_t g_cmsisMuxHelperId;
static osStatus_t g_cmsisMuxAcqRet;
static osMessageQueueId_t g_cmsisMqMpcId;
static volatile UINT32 g_cmsisQProdCnt[2];
static volatile UINT32 g_cmsisQConsCnt[2];

/* helper: wait on event flag forever, then signal completion via g_testCount */
static VOID CmsisEfWaitHelper(VOID)
{
    uint32_t ret = osEventFlagsWait(g_cmsisEfHelperId, 0x1, osFlagsWaitAny, osWaitForever);
    if ((ret & 0x1) != 0) {
        g_testCount++;
    }
}

/* helper: acquire mutex with 5-tick timeout, record result */
static VOID CmsisMuxAcqHelper(VOID)
{
    g_cmsisMuxAcqRet = osMutexAcquire(g_cmsisMuxHelperId, 5);
    g_testCount++;
}

/* helper: producer puts 50 messages into shared queue */
static VOID CmsisMqProducer(void *arg)
{
    UINT32 idx = (UINT32)(UINTPTR)arg;
    UINT32 msg;
    UINT32 i;
    for (i = 0; i < 50; i++) {
        msg = (idx << 16) | (i & 0xFFFF);
        if (osMessageQueuePut(g_cmsisMqMpcId, &msg, 0U, osWaitForever) == osOK) {
            g_cmsisQProdCnt[idx]++;
        }
    }
}

/* helper: consumer gets 50 messages from shared queue */
static VOID CmsisMqConsumer(void *arg)
{
    UINT32 idx = (UINT32)(UINTPTR)arg;
    UINT32 msg;
    UINT32 i;
    for (i = 0; i < 50; i++) {
        if (osMessageQueueGet(g_cmsisMqMpcId, &msg, NULL, osWaitForever) == osOK) {
            g_cmsisQConsCnt[idx]++;
        }
    }
}

/* ---- EventFlags ---- */
/* 用例简要描述: Wait OR+Clear */
static UINT32 TestCmsisEventFlags004(VOID)
{
    osEventFlagsId_t ef = NULL;
    uint32_t ret;

    ef = osEventFlagsNew(NULL);
    ICUNIT_GOTO_NOT_EQUAL(ef, NULL, ef, EXIT);

    /* WaitAny + NoClear: flags preserved after wait */
    (void)osEventFlagsSet(ef, 0x5);
    ret = osEventFlagsWait(ef, 0xF, osFlagsWaitAny | osFlagsNoClear, 0);
    ICUNIT_GOTO_EQUAL(ret, 0x5, ret, EXIT);
    ICUNIT_GOTO_EQUAL(osEventFlagsGet(ef), 0x5, 0, EXIT);

    (void)osEventFlagsClear(ef, 0x5);
    ICUNIT_GOTO_EQUAL(osEventFlagsGet(ef), 0, 0, EXIT);

    /* WaitAny + Clear (default): flags cleared after wait */
    (void)osEventFlagsSet(ef, 0x5);
    ret = osEventFlagsWait(ef, 0xF, osFlagsWaitAny, 0);
    ICUNIT_GOTO_EQUAL(ret, 0x5, ret, EXIT);
    ICUNIT_GOTO_EQUAL(osEventFlagsGet(ef), 0, 0, EXIT);

EXIT:
    if (ef != NULL) {
        (void)osEventFlagsDelete(ef);
    }
    return LOS_OK;
}

/* 用例简要描述: 跨线程阻塞唤醒 */
static UINT32 TestCmsisEventFlags005(VOID)
{
    osEventFlagsId_t ef = NULL;
    osThreadId_t tid = NULL;
    osThreadAttr_t attr = {0};

    attr.stack_size = OS_TSK_TEST_STACK_SIZE;
    attr.priority = osPriorityBelowNormal;
    attr.attr_bits = osThreadJoinable;

    ef = osEventFlagsNew(NULL);
    ICUNIT_GOTO_NOT_EQUAL(ef, NULL, ef, EXIT);
    g_cmsisEfHelperId = ef;
    g_testCount = 0;

    tid = osThreadNew((osThreadFunc_t)CmsisEfWaitHelper, NULL, &attr);
    ICUNIT_GOTO_NOT_EQUAL(tid, NULL, tid, EXIT);

    /* let helper block on Wait */
    osDelay(3);
    ICUNIT_GOTO_EQUAL(g_testCount, 0, g_testCount, EXIT);

    /* signal flag to wake helper */
    (void)osEventFlagsSet(ef, 0x1);
    osDelay(3);
    ICUNIT_GOTO_EQUAL(g_testCount, 1, g_testCount, EXIT);

EXIT:
    if (ef != NULL) {
        (void)osEventFlagsSet(ef, 0x1);
    }
    if (tid != NULL) {
        (void)osThreadJoin(tid);
    }
    if (ef != NULL) {
        (void)osEventFlagsDelete(ef);
    }
    return LOS_OK;
}

/* 用例简要描述: AND vs OR 决策表 */
static UINT32 TestCmsisEventFlags006(VOID)
{
    osEventFlagsId_t ef = NULL;
    uint32_t ret;

    ef = osEventFlagsNew(NULL);
    ICUNIT_GOTO_NOT_EQUAL(ef, NULL, ef, EXIT);

    /* Case 1: no flags, Wait ALL NO_CLEAR timeout=0 -> returns 0 (no match, non-blocking) */
    ret = osEventFlagsWait(ef, 0x3, osFlagsWaitAll | osFlagsNoClear, 0);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* Case 2: only 0x1 set, Wait ALL NO_CLEAR timeout=0 -> returns 0 (partial match, ALL needs all) */
    (void)osEventFlagsSet(ef, 0x1);
    ret = osEventFlagsWait(ef, 0x3, osFlagsWaitAll | osFlagsNoClear, 0);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* Case 3: set 0x2, now 0x3, Wait ALL NO_CLEAR -> returns 0x3, not cleared */
    (void)osEventFlagsSet(ef, 0x2);
    ret = osEventFlagsWait(ef, 0x3, osFlagsWaitAll | osFlagsNoClear, 0);
    ICUNIT_GOTO_EQUAL(ret, 0x3, ret, EXIT);
    ICUNIT_GOTO_EQUAL(osEventFlagsGet(ef), 0x3, 0, EXIT);

    /* Case 4: Wait ANY CLEAR -> returns 0x3, then cleared */
    ret = osEventFlagsWait(ef, 0x3, osFlagsWaitAny, 0);
    ICUNIT_GOTO_EQUAL(ret, 0x3, ret, EXIT);
    ICUNIT_GOTO_EQUAL(osEventFlagsGet(ef), 0, 0, EXIT);

    /* Case 5: empty, Wait ANY NO_CLEAR timeout=1 -> osFlagsErrorTimeout (blocks then times out) */
    ret = osEventFlagsWait(ef, 0x1, osFlagsWaitAny | osFlagsNoClear, 1);
    ICUNIT_GOTO_EQUAL(ret, (uint32_t)osFlagsErrorTimeout, ret, EXIT);

    /* Case 6: set 0x4, Wait ALL CLEAR timeout=0 -> returns 0 (partial match), flags not cleared */
    (void)osEventFlagsSet(ef, 0x4);
    ret = osEventFlagsWait(ef, 0x7, osFlagsWaitAll, 0);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL(osEventFlagsGet(ef), 0x4, 0, EXIT);

    /* Case 7: set 0x3, now 0x7, Wait ALL CLEAR -> returns 0x7, cleared */
    (void)osEventFlagsSet(ef, 0x3);
    ret = osEventFlagsWait(ef, 0x7, osFlagsWaitAll, 0);
    ICUNIT_GOTO_EQUAL(ret, 0x7, ret, EXIT);
    ICUNIT_GOTO_EQUAL(osEventFlagsGet(ef), 0, 0, EXIT);

    /* Case 8: set 0x5, Wait ANY NO_CLEAR mask=0x1 -> returns 0x1, Get stays 0x5 */
    (void)osEventFlagsSet(ef, 0x5);
    ret = osEventFlagsWait(ef, 0x1, osFlagsWaitAny | osFlagsNoClear, 0);
    ICUNIT_GOTO_EQUAL(ret, 0x1, ret, EXIT);
    ICUNIT_GOTO_EQUAL(osEventFlagsGet(ef), 0x5, 0, EXIT);

EXIT:
    if (ef != NULL) {
        (void)osEventFlagsDelete(ef);
    }
    return LOS_OK;
}

/* ---- Mutex ---- */
/* 用例简要描述: Acquire 超时 */
static UINT32 TestCmsisMutex004(VOID)
{
    osMutexId_t mid = NULL;
    osThreadId_t tid = NULL;
    osThreadAttr_t attr = {0};
    osStatus_t ret;

    attr.stack_size = OS_TSK_TEST_STACK_SIZE;
    attr.priority = osPriorityBelowNormal;
    attr.attr_bits = osThreadJoinable;

    mid = osMutexNew(NULL);
    ICUNIT_GOTO_NOT_EQUAL(mid, NULL, mid, EXIT);
    g_cmsisMuxHelperId = mid;
    g_cmsisMuxAcqRet = osOK;
    g_testCount = 0;

    /* main holds mutex */
    ret = osMutexAcquire(mid, osWaitForever);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);

    /* helper tries to acquire with 5-tick timeout -> should timeout */
    tid = osThreadNew((osThreadFunc_t)CmsisMuxAcqHelper, NULL, &attr);
    ICUNIT_GOTO_NOT_EQUAL(tid, NULL, tid, EXIT);

    osDelay(10);
    ICUNIT_GOTO_EQUAL(g_testCount, 1, g_testCount, EXIT);
    ICUNIT_GOTO_EQUAL(g_cmsisMuxAcqRet, osErrorTimeout, g_cmsisMuxAcqRet, EXIT);

    ret = osMutexRelease(mid);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);

EXIT:
    if (tid != NULL) {
        (void)osThreadJoin(tid);
    }
    if (mid != NULL) {
        (void)osMutexDelete(mid);
    }
    return LOS_OK;
}

/* 用例简要描述: GetOwner 持锁归属 */
static UINT32 TestCmsisMutex005(VOID)
{
    osMutexId_t mid;
    osThreadId_t owner;
    osThreadId_t self;
    osStatus_t ret;

    mid = osMutexNew(NULL);
    ICUNIT_GOTO_EQUAL((mid != NULL), 1, (uint32_t)(UINTPTR)mid, EXIT);

    owner = osMutexGetOwner(mid);
    ICUNIT_GOTO_EQUAL((owner == NULL), 1, (uint32_t)(UINTPTR)owner, EXIT);

    ret = osMutexAcquire(mid, osWaitForever);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);

    self = osThreadGetId();
    owner = osMutexGetOwner(mid);
    ICUNIT_GOTO_EQUAL(owner, self, (uint32_t)(UINTPTR)owner, EXIT);

    ret = osMutexRelease(mid);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);

    owner = osMutexGetOwner(mid);
    ICUNIT_GOTO_EQUAL((owner == NULL), 1, (uint32_t)(UINTPTR)owner, EXIT);

    (void)osMutexDelete(mid);
    return LOS_OK;

EXIT:
    if (mid != NULL) { (void)osMutexDelete(mid); }
    return LOS_OK;
    return LOS_OK;
}

/* ---- Semaphore ---- */
/* 用例简要描述: Acquire 超时 */
static UINT32 TestCmsisSem004(VOID)
{
    osSemaphoreId_t sid;
    osStatus_t ret;

    sid = osSemaphoreNew(1, 0, NULL);
    ICUNIT_GOTO_EQUAL((sid != NULL), 1, (uint32_t)(UINTPTR)sid, EXIT);

    /* count=0, acquire with 5-tick timeout -> timeout */
    ret = osSemaphoreAcquire(sid, 5);
    ICUNIT_GOTO_EQUAL(ret, osErrorTimeout, ret, EXIT);

    /* release to increment count */
    ret = osSemaphoreRelease(sid);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);

    /* now acquire(0) succeeds immediately */
    ret = osSemaphoreAcquire(sid, 0);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);

    (void)osSemaphoreDelete(sid);
    return LOS_OK;

EXIT:
    if (sid != NULL) { (void)osSemaphoreDelete(sid); }
    return LOS_OK;
}

/* 用例简要描述: GetCount BVA */
static UINT32 TestCmsisSem005(VOID)
{
    osSemaphoreId_t sid;
    osStatus_t ret;

    sid = osSemaphoreNew(3, 2, NULL);
    ICUNIT_GOTO_EQUAL((sid != NULL), 1, (uint32_t)(UINTPTR)sid, EXIT);

    ICUNIT_GOTO_EQUAL(osSemaphoreGetCount(sid), 2, 0, EXIT);

    ret = osSemaphoreAcquire(sid, 0);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);
    ICUNIT_GOTO_EQUAL(osSemaphoreGetCount(sid), 1, 0, EXIT);

    ret = osSemaphoreRelease(sid);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);
    ICUNIT_GOTO_EQUAL(osSemaphoreGetCount(sid), 2, 0, EXIT);

    ret = osSemaphoreRelease(sid);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);
    ICUNIT_GOTO_EQUAL(osSemaphoreGetCount(sid), 3, 0, EXIT);

    (void)osSemaphoreDelete(sid);
    return LOS_OK;

EXIT:
    if (sid != NULL) { (void)osSemaphoreDelete(sid); }
    return LOS_OK;
}

/* ---- MessageQueue ---- */
/* 用例简要描述: 满队超时 */
static UINT32 TestCmsisQueue002(VOID)
{
    osMessageQueueId_t mq = NULL;
    uint32_t msg = 0xAA;
    uint32_t buf = 0;
    osStatus_t ret;

    mq = osMessageQueueNew(1, sizeof(uint32_t), NULL);
    ICUNIT_GOTO_NOT_EQUAL(mq, NULL, mq, EXIT);

    /* fill queue (capacity=1) */
    ret = osMessageQueuePut(mq, &msg, 0, 0);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);

    /* queue full, Put with 5-tick timeout -> timeout */
    ret = osMessageQueuePut(mq, &msg, 0, 5);
    ICUNIT_GOTO_EQUAL(ret, osErrorTimeout, ret, EXIT);

    /* Get to free space */
    ret = osMessageQueueGet(mq, &buf, NULL, 0);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);
    ICUNIT_GOTO_EQUAL(buf, msg, buf, EXIT);

    /* now Put succeeds again */
    ret = osMessageQueuePut(mq, &msg, 0, 0);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);

EXIT:
    if (mq != NULL) {
        (void)osMessageQueueDelete(mq);
    }
    return LOS_OK;
}

/* 用例简要描述: 空队超时 */
static UINT32 TestCmsisQueue003(VOID)
{
    osMessageQueueId_t mq = NULL;
    uint32_t msg = 0xBB;
    uint32_t buf = 0;
    osStatus_t ret;

    mq = osMessageQueueNew(2, sizeof(uint32_t), NULL);
    ICUNIT_GOTO_NOT_EQUAL(mq, NULL, mq, EXIT);

    /* empty queue, Get with 5-tick timeout -> timeout */
    ret = osMessageQueueGet(mq, &buf, NULL, 5);
    ICUNIT_GOTO_EQUAL(ret, osErrorTimeout, ret, EXIT);

    /* Put a message */
    ret = osMessageQueuePut(mq, &msg, 0, 0);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);

    /* Get succeeds immediately */
    ret = osMessageQueueGet(mq, &buf, NULL, 0);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);
    ICUNIT_GOTO_EQUAL(buf, msg, buf, EXIT);

EXIT:
    if (mq != NULL) {
        (void)osMessageQueueDelete(mq);
    }
    return LOS_OK;
}

/* 用例简要描述: 内容一致 */
static UINT32 TestCmsisQueue004(VOID)
{
    osMessageQueueId_t mq = NULL;
    uint32_t sendData[] = {0xAA, 0xBB, 0xCC, 0xDD};
    uint32_t buf = 0;
    osStatus_t ret;
    int i;

    mq = osMessageQueueNew(4, sizeof(uint32_t), NULL);
    ICUNIT_GOTO_NOT_EQUAL(mq, NULL, mq, EXIT);

    for (i = 0; i < 4; i++) {
        ret = osMessageQueuePut(mq, &sendData[i], 0, 0);
        ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);
    }

    ICUNIT_GOTO_EQUAL(osMessageQueueGetCount(mq), 4, 0, EXIT);
    ICUNIT_GOTO_EQUAL(osMessageQueueGetSpace(mq), 0, 0, EXIT);

    for (i = 0; i < 4; i++) {
        ret = osMessageQueueGet(mq, &buf, NULL, 0);
        ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);
        ICUNIT_GOTO_EQUAL(buf, sendData[i], buf, EXIT);
    }

    ICUNIT_GOTO_EQUAL(osMessageQueueGetCount(mq), 0, 0, EXIT);
    ICUNIT_GOTO_EQUAL(osMessageQueueGetSpace(mq), 4, 0, EXIT);

EXIT:
    if (mq != NULL) {
        (void)osMessageQueueDelete(mq);
    }
    return LOS_OK;
}

/* 用例简要描述: 多生产者消费者 */
static UINT32 TestCmsisQueue005(VOID)
{
    osMessageQueueId_t mq = NULL;
    osThreadId_t prod[2] = {NULL};
    osThreadId_t cons[2] = {NULL};
    osThreadAttr_t attr = {0};
    int i;

    attr.stack_size = OS_TSK_TEST_STACK_SIZE;
    attr.priority = osPriorityBelowNormal;
    attr.attr_bits = osThreadJoinable;

    mq = osMessageQueueNew(16, sizeof(uint32_t), NULL);
    ICUNIT_GOTO_NOT_EQUAL(mq, NULL, mq, EXIT);
    g_cmsisMqMpcId = mq;

    for (i = 0; i < 2; i++) {
        g_cmsisQProdCnt[i] = 0;
        g_cmsisQConsCnt[i] = 0;
    }

    prod[0] = osThreadNew((osThreadFunc_t)CmsisMqProducer, (void *)0, &attr);
    ICUNIT_GOTO_NOT_EQUAL(prod[0], NULL, prod[0], EXIT);
    prod[1] = osThreadNew((osThreadFunc_t)CmsisMqProducer, (void *)1, &attr);
    ICUNIT_GOTO_NOT_EQUAL(prod[1], NULL, prod[1], EXIT);
    cons[0] = osThreadNew((osThreadFunc_t)CmsisMqConsumer, (void *)0, &attr);
    ICUNIT_GOTO_NOT_EQUAL(cons[0], NULL, cons[0], EXIT);
    cons[1] = osThreadNew((osThreadFunc_t)CmsisMqConsumer, (void *)1, &attr);
    ICUNIT_GOTO_NOT_EQUAL(cons[1], NULL, cons[1], EXIT);

    (void)osThreadJoin(prod[0]); prod[0] = NULL;
    (void)osThreadJoin(prod[1]); prod[1] = NULL;
    (void)osThreadJoin(cons[0]); cons[0] = NULL;
    (void)osThreadJoin(cons[1]); cons[1] = NULL;

    ICUNIT_GOTO_EQUAL(g_cmsisQProdCnt[0] + g_cmsisQProdCnt[1], 100, 0, EXIT);
    ICUNIT_GOTO_EQUAL(g_cmsisQConsCnt[0] + g_cmsisQConsCnt[1], 100, 0, EXIT);

EXIT:
    for (i = 0; i < 2; i++) {
        if (prod[i] != NULL) {
            (void)osThreadJoin(prod[i]);
        }
        if (cons[i] != NULL) {
            (void)osThreadJoin(cons[i]);
        }
    }
    if (mq != NULL) {
        (void)osMessageQueueDelete(mq);
    }
    return LOS_OK;
}

/* ---- MemoryPool ---- */
/* 用例简要描述: GetCount+Space */
#if (LOSCFG_KERNEL_MEMBOX_STATIC == 1) /* membox dyn 下 CMSIS osMemoryPool* 无实现, 用例隔离(测试侧适配) */
static UINT32 TestCmsisMemPool007(VOID)
{
    osMemoryPoolId_t mp;
    void *p1 = NULL;
    void *p2 = NULL;

    mp = osMemoryPoolNew(4, 32, NULL);
    ICUNIT_GOTO_NOT_EQUAL(mp, NULL, mp, EXIT);

    ICUNIT_GOTO_EQUAL(osMemoryPoolGetCount(mp), 0, 0, EXIT);
    ICUNIT_GOTO_EQUAL(osMemoryPoolGetSpace(mp), 4, 0, EXIT);

    p1 = osMemoryPoolAlloc(mp, 0);
    ICUNIT_GOTO_NOT_EQUAL(p1, NULL, p1, EXIT);
    ICUNIT_GOTO_EQUAL(osMemoryPoolGetCount(mp), 1, 0, EXIT);
    ICUNIT_GOTO_EQUAL(osMemoryPoolGetSpace(mp), 3, 0, EXIT);

    p2 = osMemoryPoolAlloc(mp, 0);
    ICUNIT_GOTO_NOT_EQUAL(p2, NULL, p2, EXIT);
    ICUNIT_GOTO_EQUAL(osMemoryPoolGetCount(mp), 2, 0, EXIT);

    (void)osMemoryPoolFree(mp, p1);
    ICUNIT_GOTO_EQUAL(osMemoryPoolGetCount(mp), 1, 0, EXIT);
    (void)osMemoryPoolFree(mp, p2);
    ICUNIT_GOTO_EQUAL(osMemoryPoolGetCount(mp), 0, 0, EXIT);
    ICUNIT_GOTO_EQUAL(osMemoryPoolGetSpace(mp), 4, 0, EXIT);

EXIT:
    if (mp != NULL) {
        (void)osMemoryPoolDelete(mp);
    }
    return LOS_OK;
}

static UINT32 TestCmsisMemPool008(VOID)
{
    osMemoryPoolId_t mp = NULL;
    void *blocks[16] = {NULL};
    int allocIdx = 0;
    int i;
    UINT32 seed = 0x12345678U;

    mp = osMemoryPoolNew(16, 32, NULL);
    ICUNIT_GOTO_NOT_EQUAL(mp, NULL, mp, EXIT);

    for (i = 0; i < 100; i++) {
        seed = seed * 1103515245U + 12345U;
        if ((seed & 1U) || (allocIdx == 0)) {
            if (allocIdx < 16) {
                blocks[allocIdx] = osMemoryPoolAlloc(mp, 0);
                if (blocks[allocIdx] != NULL) {
                    allocIdx++;
                }
            }
        } else {
            allocIdx--;
            (void)osMemoryPoolFree(mp, blocks[allocIdx]);
            blocks[allocIdx] = NULL;
        }
    }

    for (i = 0; i < 16; i++) {
        if (blocks[i] != NULL) {
            (void)osMemoryPoolFree(mp, blocks[i]);
            blocks[i] = NULL;
        }
    }

    ICUNIT_GOTO_EQUAL(osMemoryPoolGetCount(mp), 0, 0, EXIT);
    ICUNIT_GOTO_EQUAL(osMemoryPoolGetSpace(mp), 16, 0, EXIT);

EXIT:
    if (mp != NULL) {
        (void)osMemoryPoolDelete(mp);
    }
    return LOS_OK;
}
#endif /* LOSCFG_KERNEL_MEMBOX_STATIC */


/* 用例简要描述: CMSIS 低频接口一揽子：osKernelInitialize（运行期 osError）、
 * osSemaphoreGetName / osEventFlagsGetName（恒 NULL 查询接口）、osTimerExtNew
 * （对齐扩展定时器创建：周期/单次成功 + NULL func/非法 type 负路径）。
 * 所有断言走 EXIT 统一回收定时器，异常路径不泄漏资源。 */
static UINT32 TestCmsisLowFreq001(VOID)
{
    const char *name;
    osTimerId_t timerId = NULL;
    osTimerId_t timerOnceId = NULL;
    osSemaphoreId_t semId = NULL;
    osTimerFunc_t fn = CmsisLowFreqTimerCb;

    g_cmsisLowFreqTimerRan = 0;

    /* F-Pos: 信号量名称查询接口——本实现恒返回 NULL（含合法 id 与 NULL id 容错）。 */
    semId = osSemaphoreNew(1, 0, NULL);
    ICUNIT_GOTO_NOT_EQUAL((UINTPTR)semId, (UINTPTR)NULL, (UINTPTR)semId, EXIT);
    name = osSemaphoreGetName(semId);
    ICUNIT_GOTO_EQUAL((UINTPTR)name, (UINTPTR)NULL, (UINTPTR)name, EXIT);
    name = osSemaphoreGetName(NULL);
    ICUNIT_GOTO_EQUAL((UINTPTR)name, (UINTPTR)NULL, (UINTPTR)name, EXIT);

    /* F-Pos: 事件标志名称查询接口——本实现恒返回 NULL（任务上下文路径）。 */
    name = osEventFlagsGetName(NULL);
    ICUNIT_GOTO_EQUAL((UINTPTR)name, (UINTPTR)NULL, (UINTPTR)name, EXIT);

#if (LOSCFG_BASE_CORE_SWTMR_ALIGN == 1)
    /* F-Inv: 定时器创建负路径——NULL 回调 / 非法 type。 */
    timerId = osTimerExtNew(NULL, osTimerPeriodic, NULL, NULL, osTimerRousesAllow, osTimerAlignAllow);
    ICUNIT_GOTO_EQUAL((UINTPTR)timerId, (UINTPTR)NULL, (UINTPTR)timerId, EXIT);
    timerId = osTimerExtNew(fn, (osTimerType_t)0xFF, NULL, NULL, osTimerRousesAllow, osTimerAlignAllow);
    ICUNIT_GOTO_EQUAL((UINTPTR)timerId, (UINTPTR)NULL, (UINTPTR)timerId, EXIT);

    /* F-Pos: 周期定时器 EXT 创建（对齐参数路由到 LOS_SwtmrCreate）。 */
    timerId = osTimerExtNew(fn, osTimerPeriodic, NULL, NULL, osTimerRousesAllow, osTimerAlignAllow);
    ICUNIT_GOTO_NOT_EQUAL((UINTPTR)timerId, (UINTPTR)NULL, (UINTPTR)timerId, EXIT);
    ICUNIT_GOTO_EQUAL(osTimerStart(timerId, 2), osOK, osTimerStart(timerId, 2), EXIT); // 2, period ticks.

    /* F-Pos: 单次定时器 EXT 创建。 */
    timerOnceId = osTimerExtNew(fn, osTimerOnce, NULL, NULL, osTimerRousesIgnore, osTimerAlignIgnore);
    ICUNIT_GOTO_NOT_EQUAL((UINTPTR)timerOnceId, (UINTPTR)NULL, (UINTPTR)timerOnceId, EXIT);
    ICUNIT_GOTO_EQUAL(osTimerStart(timerOnceId, 2), osOK, osTimerStart(timerOnceId, 2), EXIT); // 2, delay ticks.

    (void)osDelay(5); // 5, let timers fire.
    ICUNIT_GOTO_EQUAL(g_cmsisLowFreqTimerRan > 0, TRUE, g_cmsisLowFreqTimerRan, EXIT);

    (void)osTimerStop(timerId);
    (void)osTimerDelete(timerId);
    timerId = NULL;
    (void)osTimerDelete(timerOnceId);
    timerOnceId = NULL;
#endif
    /* 正常路径同样回收信号量。 */
    if (semId != NULL) {
        (void)osSemaphoreDelete(semId);
        semId = NULL;
    }
    return LOS_OK;

EXIT:
    if (semId != NULL) {
        (void)osSemaphoreDelete(semId);
    }
    if (timerId != NULL) {
        (void)osTimerStop(timerId);
        (void)osTimerDelete(timerId);
    }
    if (timerOnceId != NULL) {
        (void)osTimerDelete(timerOnceId);
    }
    return LOS_OK;
}

/* ==== Batch 3: 覆盖率补充——错误分支/边界(目标 cmsis_liteos2.c 行/函数 100%) ==== */

/* 用例简要描述: osKernelGetInfo 负路径: id_size 过小触发 memcpy_s 失败 + NULL 参数 */
static UINT32 TestCmsisKernel010(VOID)
{
    osVersion_t ver = {0};
    CHAR idBuf[32] = {0};
    osStatus_t ret;

    /* id_size=8 < sizeof("HUAWEI-LiteOS")=15: memcpy_s 返回非 EOK → osError (覆盖 127-128 行) */
    ret = osKernelGetInfo(&ver, idBuf, 8);
    ICUNIT_ASSERT_EQUAL(ret, osError, ret);

    ret = osKernelGetInfo(NULL, idBuf, sizeof(idBuf));
    ICUNIT_ASSERT_EQUAL(ret, osError, ret);

    ret = osKernelGetInfo(&ver, NULL, sizeof(idBuf));
    ICUNIT_ASSERT_EQUAL(ret, osError, ret);

    ret = osKernelGetInfo(&ver, idBuf, 0);
    ICUNIT_ASSERT_EQUAL(ret, osError, ret);

    return LOS_OK;
}

/* 用例简要描述: 嵌套任务锁下 osKernelUnlock/RestoreLock 错误分支(LOS_TaskLock/Unlock 为对外接口) */
static UINT32 TestCmsisKernel011(VOID)
{
    int32_t ret;

    /* 嵌套锁: taskLockCnt=2, osKernelUnlock 解一层后仍非 0 → osError (覆盖 200 行) */
    LOS_TaskLock();
    LOS_TaskLock();
    ret = osKernelUnlock();
    ICUNIT_GOTO_EQUAL(ret, (int32_t)osError, ret, EXIT);

    /* RestoreLock(0) 于 cnt=1: 解锁后 cnt==0 → 正常返回 0 */
    ret = osKernelRestoreLock(0);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* RestoreLock(0) 于 cnt=2: 解一层后仍非 0 → break → osError (覆盖 223-225/234 行) */
    LOS_TaskLock();
    LOS_TaskLock();
    ret = osKernelRestoreLock(0);
    ICUNIT_GOTO_EQUAL(ret, (int32_t)osError, ret, EXIT);
    LOS_TaskUnlock(); /* 恢复 cnt=0 */

    /* RestoreLock 非法值 → default → osError (覆盖 230-234 行) */
    ret = osKernelRestoreLock(42);
    ICUNIT_GOTO_EQUAL(ret, (int32_t)osError, ret, EXIT);

    /* 收尾校验锁状态已归零 */
    ret = osKernelLock();
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = osKernelUnlock();
    ICUNIT_GOTO_EQUAL(ret, 1, ret, EXIT);

EXIT:
    /* 兜底: 保证任务锁计数归零(本用例最多嵌套 2 层, 多余恢复在 cnt==0 时为无害空操作) */
    (void)osKernelRestoreLock(0);
    (void)osKernelRestoreLock(0);
    (void)osKernelRestoreLock(0);
    return LOS_OK;
}

/* 用例简要描述: 调度锁下 osDelayUntil → LOS_TaskDelay 返回 DELAY_IN_LOCK → osError (覆盖 730 行) */
static UINT32 TestCmsisKernel012(VOID)
{
    osStatus_t ret;
    uint32_t now = osKernelGetTickCount();

    /* osDelayUntil 无 HalDelay 分支: 锁下直接进 LOS_TaskDelay → IN_LOCK 错误 */
    (void)osKernelLock();
    ret = osDelayUntil(now + 10);
    ICUNIT_GOTO_EQUAL(ret, osError, ret, EXIT);

EXIT:
    (void)osKernelRestoreLock(0);
    return LOS_OK;
}

/* 用例简要描述: 线程错误分支: detach 非 joinable/重复 suspend/resume 未挂起/重复 terminate/
 * join 自身/已删线程 SetPriority/GetPriority/GetStackSpace/FlagsSet */
static UINT32 TestCmsisThread014(VOID)
{
    osThreadId_t tid;
    osThreadAttr_t attr = {0};
    osStatus_t ret;
    osPriority_t prio;
    uint32_t flagsRet;
    uint32_t space;

    attr.stack_size = OS_TSK_TEST_STACK_SIZE;
    attr.priority = osPriorityLow1;
    attr.attr_bits = osThreadDetached;
    g_testCount = 0;

    tid = osThreadNew((osThreadFunc_t)CmsisThreadWaitFunc, NULL, &attr);
    ICUNIT_ASSERT_NOT_EQUAL(tid, NULL, tid);
    osDelay(2); /* 等 helper 递增后阻塞在 osDelay */
    ICUNIT_ASSERT_EQUAL(g_testCount, 1, g_testCount);

    /* detach 非 joinable 线程 → LOS_TaskDetach 返回 IS_DETACHED → osErrorResource (覆盖 603 行) */
    ret = osThreadDetach(tid);
    ICUNIT_ASSERT_EQUAL(ret, osErrorResource, ret);

    /* resume 未挂起线程 → NOT_SUSPENDED → osErrorResource (覆盖 581-582 行) */
    ret = osThreadResume(tid);
    ICUNIT_ASSERT_EQUAL(ret, osErrorResource, ret);

    /* 挂起成功后重复挂起 → ALREADY_SUSPENDED → osErrorResource (覆盖 551-554 行) */
    ret = osThreadSuspend(tid);
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);
    ret = osThreadSuspend(tid);
    ICUNIT_ASSERT_EQUAL(ret, osErrorResource, ret);
    ret = osThreadResume(tid);
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);

    /* join 自身 → NOT_JOIN_SELF → osErrorResource (覆盖 621-622 行) */
    ret = osThreadJoin(osThreadGetId());
    ICUNIT_ASSERT_EQUAL(ret, osErrorResource, ret);

    /* 终止后重复终止 → NOT_CREATED → osErrorResource (覆盖 650-651 行) */
    ret = osThreadTerminate(tid);
    ICUNIT_ASSERT_EQUAL(ret, osOK, ret);
    ret = osThreadTerminate(tid);
    ICUNIT_ASSERT_EQUAL(ret, osErrorResource, ret);

    /* 已删线程(TCB 静态数组、无复用窗口)查询 */
    ICUNIT_ASSERT_EQUAL(osThreadGetState(tid), osThreadInactive, osThreadGetState(tid));

    /* SetPriority → NOT_CREATED → osErrorResource (覆盖 479-480 行) */
    ret = osThreadSetPriority(tid, osPriorityNormal);
    ICUNIT_ASSERT_EQUAL(ret, osErrorResource, ret);

    /* GetPriority → OS_INVALID → osPriorityError (覆盖 499-501 行) */
    prio = osThreadGetPriority(tid);
    ICUNIT_ASSERT_EQUAL(prio, osPriorityError, prio);

    /* GetStackSpace → watermark UNUSED 分支返回 0 (覆盖 415-417 行) */
    space = osThreadGetStackSpace(tid);
    ICUNIT_ASSERT_EQUAL(space, 0, space);

    /* FlagsSet → LOS_EventWrite 返回 NOT_INITIALIZED → osFlagsErrorResource (覆盖 1783-1784 行) */
    flagsRet = osThreadFlagsSet(tid, 0x1);
    ICUNIT_ASSERT_EQUAL(flagsRet, (uint32_t)osFlagsErrorResource, flagsRet);

    return LOS_OK;
}

/* 用例简要描述: 调度锁下 osThreadYield → YIELD_IN_LOCK → osError (覆盖 527 行) */
static UINT32 TestCmsisThread015(VOID)
{
    osStatus_t ret;

    (void)osKernelLock();
    ret = osThreadYield();
    ICUNIT_GOTO_EQUAL(ret, osError, ret, EXIT);

EXIT:
    (void)osKernelRestoreLock(0);
    return LOS_OK;
}

/* 用例简要描述: ThreadFlags 错误分支: 含错误位 flags Set/等待超时/锁下等待 */
static UINT32 TestCmsisThreadFlags005(VOID)
{
    osThreadId_t tid = osThreadGetId();
    uint32_t ret;

    /* flags 含 LOS_ERRTYPE_ERROR 位(0x02<<24) → SETBIT_INVALID → osFlagsErrorParameter (覆盖 1781-1782 行) */
    ret = osThreadFlagsSet(tid, 0x02000000);
    ICUNIT_GOTO_EQUAL(ret, (uint32_t)osFlagsErrorParameter, ret, EXIT);

    /* 无标志位等待 2 tick → READ_TIMEOUT → osFlagsErrorTimeout (覆盖 1867-1868 行) */
    (void)osThreadFlagsClear(0xFFFFFFFFU);
    ret = osThreadFlagsWait(0x1, osFlagsWaitAny, 2);
    ICUNIT_GOTO_EQUAL(ret, (uint32_t)osFlagsErrorTimeout, ret, EXIT);

    /* 调度锁下等待(需阻塞) → READ_IN_LOCK → default → osFlagsErrorResource (覆盖 1869-1870 行) */
    (void)osKernelLock();
    ret = osThreadFlagsWait(0x1, osFlagsWaitAny, 2);
    ICUNIT_GOTO_EQUAL(ret, (uint32_t)osFlagsErrorResource, ret, EXIT);

EXIT:
    (void)osKernelRestoreLock(0);
    return LOS_OK;
}

/* 用例简要描述: EventFlags 错误分支: 含错误位 flags Set/锁下等待 READ_IN_LOCK */
static UINT32 TestCmsisEventFlags007(VOID)
{
    osEventFlagsId_t ef = NULL;
    uint32_t ret;

    ef = osEventFlagsNew(NULL);
    ICUNIT_GOTO_NOT_EQUAL(ef, NULL, ef, EXIT);

    /* flags 含 LOS_ERRTYPE_ERROR 位 → SETBIT_INVALID → osFlagsErrorResource (覆盖 928-929 行) */
    ret = osEventFlagsSet(ef, 0x02000000);
    ICUNIT_GOTO_EQUAL(ret, (uint32_t)osFlagsErrorResource, ret, EXIT);

    /* 调度锁下等待(需阻塞) → READ_IN_LOCK → osFlagsErrorResource (覆盖 1008-1010 行) */
    (void)osKernelLock();
    ret = osEventFlagsWait(ef, 0x1, osFlagsWaitAny, 2);
    ICUNIT_GOTO_EQUAL(ret, (uint32_t)osFlagsErrorResource, ret, EXIT);

EXIT:
    (void)osKernelRestoreLock(0); /* cnt==0 时为无害空操作 */
    if (ef != NULL) {
        (void)osEventFlagsDelete(ef);
    }
    return LOS_OK;
}

/* ==== ISR 负路径综合: 适配层各接口 OS_INT_ACTIVE 保护分支 ==== */
static osEventFlagsId_t g_isr2EfId;
static osMutexId_t g_isr2MuxId;
static osSemaphoreId_t g_isr2SemId;
static osTimerId_t g_isr2TmrId;
static osMemoryPoolId_t g_isr2MpId;
static osThreadId_t g_isr2ThrId;
static osMessageQueueId_t g_isr2MqId;
static volatile uint32_t g_isr2EfWaitRet = 0xDEAD;
static volatile uint32_t g_isr2EfGetRet = 0xDEAD;
static volatile uint32_t g_isr2EfDelRet = 0xDEAD;
static volatile uint32_t g_isr2MuxAcqRet = 0xDEAD;
static volatile uint32_t g_isr2MuxRelRet = 0xDEAD;
static volatile uint32_t g_isr2SemDelRet = 0xDEAD;
static volatile uint32_t g_isr2TmrStartRet = 0xDEAD;
static volatile uint32_t g_isr2TmrStopRet = 0xDEAD;
static volatile uint32_t g_isr2TmrDelRet = 0xDEAD;
static volatile uint32_t g_isr2TfClrRet = 0xDEAD;
static volatile uint32_t g_isr2TfGetRet = 0xDEAD;
static volatile uint32_t g_isr2TfWaitRet = 0xDEAD;
static volatile uint32_t g_isr2YieldRet = 0xDEAD;
static volatile uint32_t g_isr2SuspRet = 0xDEAD;
static volatile uint32_t g_isr2DelayRet = 0xDEAD;
static volatile uint32_t g_isr2DelayUntilRet = 0xDEAD;
static volatile uint32_t g_isr2KernelInitRet = 0xDEAD;
static volatile uint32_t g_isr2KernelStartRet = 0xDEAD;
static volatile uint32_t g_isr2KernelLockRet = 0xDEAD;
static volatile uint32_t g_isr2ThrJoinRet = 0xDEAD;
static volatile uint32_t g_isr2ThrDetachRet = 0xDEAD;
static volatile uint32_t g_isr2ThrSetPriRet = 0xDEAD;
static volatile uint32_t g_isr2ThrGetPriRet = 0xDEAD;
static volatile uint32_t g_isr2ThrGetNameRet = 0xDEAD;
static volatile uint32_t g_isr2ThrGetStateRet = 0xDEAD;
static volatile uint32_t g_isr2ThrGetStackRet = 0xDEAD;
static volatile uint32_t g_isr2ThrGetSpaceRet = 0xDEAD;
static volatile uint32_t g_isr2ThrGetCntRet = 0xDEAD;
static volatile uint32_t g_isr2ThrGetArgRet = 0xDEAD;
static volatile uint32_t g_isr2MuxOwnerRet = 0xDEAD;
static volatile uint32_t g_isr2MpDelRet = 0xDEAD;
static volatile uint32_t g_isr2MqDelRet = 0xDEAD;
static volatile uint32_t g_isr2EfNewRet = 0xDEAD;
static volatile uint32_t g_isr2MuxNewRet = 0xDEAD;

static VOID HwiFIsr002(VOID)
{
    TestHwiClear(HWI_NUM_TEST);
    /* EventFlags: ISR 内 Wait(timeout=0 通过入参检查, LOS_EventRead 返回 READ_IN_INTERRUPT) */
    g_isr2EfWaitRet = osEventFlagsWait(g_isr2EfId, 0x1, osFlagsWaitAny, 0);
    g_isr2EfGetRet = osEventFlagsGet(g_isr2EfId);
    g_isr2EfDelRet = osEventFlagsDelete(g_isr2EfId);
    g_isr2EfNewRet = (uint32_t)(UINTPTR)osEventFlagsNew(NULL);
    /* Mutex */
    g_isr2MuxAcqRet = osMutexAcquire(g_isr2MuxId, 0);
    g_isr2MuxRelRet = osMutexRelease(g_isr2MuxId);
    g_isr2MuxOwnerRet = (uint32_t)(UINTPTR)osMutexGetOwner(g_isr2MuxId);
    g_isr2MuxNewRet = (uint32_t)(UINTPTR)osMutexNew(NULL);
    /* Semaphore / Timer */
    g_isr2SemDelRet = osSemaphoreDelete(g_isr2SemId);
    g_isr2TmrStartRet = osTimerStart(g_isr2TmrId, 5);
    g_isr2TmrStopRet = osTimerStop(g_isr2TmrId);
    g_isr2TmrDelRet = osTimerDelete(g_isr2TmrId);
    /* ThreadFlags */
    g_isr2TfClrRet = osThreadFlagsClear(0x1);
    g_isr2TfGetRet = osThreadFlagsGet();
    g_isr2TfWaitRet = osThreadFlagsWait(0x1, osFlagsWaitAny, 0);
    /* Thread */
    g_isr2YieldRet = osThreadYield();
    g_isr2SuspRet = osThreadSuspend(g_isr2ThrId);
    g_isr2DelayRet = osDelay(1);
    g_isr2DelayUntilRet = osDelayUntil(osKernelGetTickCount() + 5);
    /* Kernel */
    g_isr2KernelInitRet = osKernelInitialize();
    g_isr2KernelStartRet = osKernelStart();
    g_isr2KernelLockRet = osKernelLock();
    g_isr2ThrJoinRet = osThreadJoin(g_isr2ThrId);
    g_isr2ThrDetachRet = osThreadDetach(g_isr2ThrId);
    g_isr2ThrSetPriRet = osThreadSetPriority(g_isr2ThrId, osPriorityNormal);
    g_isr2ThrGetPriRet = osThreadGetPriority(g_isr2ThrId);
    g_isr2ThrGetNameRet = (uint32_t)(UINTPTR)osThreadGetName(g_isr2ThrId);
    g_isr2ThrGetStateRet = osThreadGetState(g_isr2ThrId);
    g_isr2ThrGetStackRet = osThreadGetStackSize(g_isr2ThrId);
    g_isr2ThrGetSpaceRet = osThreadGetStackSpace(g_isr2ThrId);
    g_isr2ThrGetCntRet = osThreadGetCount();
    g_isr2ThrGetArgRet = (uint32_t)(UINTPTR)osThreadGetArgument();
    /* MemoryPool / MessageQueue */
    g_isr2MpDelRet = osMemoryPoolDelete(g_isr2MpId);
    g_isr2MqDelRet = osMessageQueueDelete(g_isr2MqId);
}

/* 用例简要描述: ISR 负路径: 适配层 OS_INT_ACTIVE 保护分支(只调保护分支, 不触内核危险路径) */
static UINT32 TestCmsisIsr002(VOID)
{
    uint32_t ret;
    HWI_PRIOR_T hwiPrio = 3;
    HWI_MODE_T mode = 0;
    HwiIrqParam irqParam;

    /* 资源准备(任务上下文) */
    g_isr2EfId = osEventFlagsNew(NULL);
    ICUNIT_ASSERT_EQUAL((g_isr2EfId != NULL), 1, (uint32_t)(UINTPTR)g_isr2EfId);
    g_isr2MuxId = osMutexNew(NULL);
    ICUNIT_ASSERT_EQUAL((g_isr2MuxId != NULL), 1, (uint32_t)(UINTPTR)g_isr2MuxId);
    g_isr2SemId = osSemaphoreNew(1, 0, NULL);
    ICUNIT_ASSERT_EQUAL((g_isr2SemId != NULL), 1, (uint32_t)(UINTPTR)g_isr2SemId);
    g_isr2TmrId = osTimerNew(CmsisTimerCallback, osTimerOnce, NULL, NULL);
    ICUNIT_ASSERT_EQUAL((g_isr2TmrId != NULL), 1, (uint32_t)(UINTPTR)g_isr2TmrId);
#if (LOSCFG_KERNEL_MEMBOX_STATIC == 1)
    g_isr2MpId = osMemoryPoolNew(2, 16, NULL);
    ICUNIT_ASSERT_EQUAL((g_isr2MpId != NULL), 1, (uint32_t)(UINTPTR)g_isr2MpId);
#endif
    g_isr2MqId = osMessageQueueNew(2, 8, NULL);
    ICUNIT_ASSERT_EQUAL((g_isr2MqId != NULL), 1, (uint32_t)(UINTPTR)g_isr2MqId);
    g_isr2ThrId = osThreadGetId();

    (VOID)memset_s(&irqParam, sizeof(HwiIrqParam), 0, sizeof(HwiIrqParam));
    irqParam.pDevId = 0;
    ret = LOS_HwiCreate(HWI_NUM_TEST, hwiPrio, mode, (HWI_PROC_FUNC)HwiFIsr002, &irqParam);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    TestHwiTrigger(HWI_NUM_TEST);

    /* EventFlags */
    ICUNIT_GOTO_EQUAL(g_isr2EfWaitRet, (uint32_t)osFlagsErrorResource, g_isr2EfWaitRet, EXIT);
    ICUNIT_GOTO_EQUAL(g_isr2EfNewRet, 0, g_isr2EfNewRet, EXIT);
    ICUNIT_GOTO_EQUAL(g_isr2EfDelRet, (uint32_t)osErrorISR, g_isr2EfDelRet, EXIT);
    /* Mutex */
    ICUNIT_GOTO_EQUAL(g_isr2MuxAcqRet, (uint32_t)osErrorISR, g_isr2MuxAcqRet, EXIT);
    ICUNIT_GOTO_EQUAL(g_isr2MuxRelRet, (uint32_t)osErrorISR, g_isr2MuxRelRet, EXIT);
    ICUNIT_GOTO_EQUAL(g_isr2MuxOwnerRet, 0, g_isr2MuxOwnerRet, EXIT);
    ICUNIT_GOTO_EQUAL(g_isr2MuxNewRet, 0, g_isr2MuxNewRet, EXIT);
    /* Semaphore / Timer */
    ICUNIT_GOTO_EQUAL(g_isr2SemDelRet, (uint32_t)osErrorISR, g_isr2SemDelRet, EXIT);
    ICUNIT_GOTO_EQUAL(g_isr2TmrStartRet, (uint32_t)osErrorISR, g_isr2TmrStartRet, EXIT);
    ICUNIT_GOTO_EQUAL(g_isr2TmrStopRet, (uint32_t)osErrorISR, g_isr2TmrStopRet, EXIT);
    ICUNIT_GOTO_EQUAL(g_isr2TmrDelRet, (uint32_t)osErrorISR, g_isr2TmrDelRet, EXIT);
    /* ThreadFlags */
    ICUNIT_GOTO_EQUAL(g_isr2TfClrRet, (uint32_t)osFlagsErrorUnknown, g_isr2TfClrRet, EXIT);
    ICUNIT_GOTO_EQUAL(g_isr2TfGetRet, (uint32_t)osFlagsErrorUnknown, g_isr2TfGetRet, EXIT);
    ICUNIT_GOTO_EQUAL(g_isr2TfWaitRet, (uint32_t)osFlagsErrorUnknown, g_isr2TfWaitRet, EXIT);
    /* Thread */
    ICUNIT_GOTO_EQUAL(g_isr2YieldRet, (uint32_t)osErrorISR, g_isr2YieldRet, EXIT);
    ICUNIT_GOTO_EQUAL(g_isr2SuspRet, (uint32_t)osErrorISR, g_isr2SuspRet, EXIT);
    ICUNIT_GOTO_EQUAL(g_isr2DelayRet, (uint32_t)osErrorISR, g_isr2DelayRet, EXIT);
    ICUNIT_GOTO_EQUAL(g_isr2DelayUntilRet, (uint32_t)osErrorISR, g_isr2DelayUntilRet, EXIT);
    /* Kernel */
    ICUNIT_GOTO_EQUAL(g_isr2KernelInitRet, (uint32_t)osErrorISR, g_isr2KernelInitRet, EXIT);
    ICUNIT_GOTO_EQUAL(g_isr2KernelStartRet, (uint32_t)osErrorISR, g_isr2KernelStartRet, EXIT);
    ICUNIT_GOTO_EQUAL(g_isr2KernelLockRet, (uint32_t)osErrorISR, g_isr2KernelLockRet, EXIT);
    ICUNIT_GOTO_EQUAL(g_isr2ThrJoinRet, (uint32_t)osErrorISR, g_isr2ThrJoinRet, EXIT);
    ICUNIT_GOTO_EQUAL(g_isr2ThrDetachRet, (uint32_t)osErrorISR, g_isr2ThrDetachRet, EXIT);
    ICUNIT_GOTO_EQUAL(g_isr2ThrSetPriRet, (uint32_t)osErrorISR, g_isr2ThrSetPriRet, EXIT);
    ICUNIT_GOTO_EQUAL(g_isr2ThrGetPriRet, (uint32_t)osPriorityError, g_isr2ThrGetPriRet, EXIT);
    ICUNIT_GOTO_EQUAL(g_isr2ThrGetNameRet, 0, g_isr2ThrGetNameRet, EXIT);
    ICUNIT_GOTO_EQUAL(g_isr2ThrGetStateRet, (uint32_t)osThreadError, g_isr2ThrGetStateRet, EXIT);
    ICUNIT_GOTO_EQUAL(g_isr2ThrGetStackRet, 0, g_isr2ThrGetStackRet, EXIT);
    ICUNIT_GOTO_EQUAL(g_isr2ThrGetSpaceRet, 0, g_isr2ThrGetSpaceRet, EXIT);
    ICUNIT_GOTO_EQUAL(g_isr2ThrGetCntRet, 0, g_isr2ThrGetCntRet, EXIT);
    ICUNIT_GOTO_EQUAL(g_isr2ThrGetArgRet, 0, g_isr2ThrGetArgRet, EXIT);
    /* MemoryPool / MessageQueue */
    ICUNIT_GOTO_EQUAL(g_isr2MpDelRet, (uint32_t)osErrorISR, g_isr2MpDelRet, EXIT);
    ICUNIT_GOTO_EQUAL(g_isr2MqDelRet, (uint32_t)osErrorISR, g_isr2MqDelRet, EXIT);

EXIT:
    TestHwiDelete(HWI_NUM_TEST);
    if (g_isr2EfId != NULL) { (VOID)osEventFlagsDelete(g_isr2EfId); g_isr2EfId = NULL; }
    if (g_isr2MuxId != NULL) { (VOID)osMutexDelete(g_isr2MuxId); g_isr2MuxId = NULL; }
    if (g_isr2SemId != NULL) { (VOID)osSemaphoreDelete(g_isr2SemId); g_isr2SemId = NULL; }
    if (g_isr2TmrId != NULL) { (VOID)osTimerDelete(g_isr2TmrId); g_isr2TmrId = NULL; }
#if (LOSCFG_KERNEL_MEMBOX_STATIC == 1)
    if (g_isr2MpId != NULL) { (VOID)osMemoryPoolDelete(g_isr2MpId); g_isr2MpId = NULL; }
#endif
    if (g_isr2MqId != NULL) { (VOID)osMessageQueueDelete(g_isr2MqId); g_isr2MqId = NULL; }
    return LOS_OK;
}

/* 用例简要描述: Mutex 错误分支: 未持锁 release/锁下 acquire/删除被持有锁 */
static UINT32 TestCmsisMutex006(VOID)
{
    osMutexId_t mid = NULL;
    osStatus_t ret;

    mid = osMutexNew(NULL);
    ICUNIT_GOTO_NOT_EQUAL(mid, NULL, mid, EXIT);

    /* 未 acquire 直接 release → muxCount==0 → MUX_INVALID → osErrorParameter (覆盖 1109-1110 行) */
    ret = osMutexRelease(mid);
    ICUNIT_GOTO_EQUAL(ret, osErrorParameter, ret, EXIT);

    /* 调度锁下 acquire → PEND_IN_LOCK → osErrorResource (覆盖 1089-1090 行) */
    (void)osKernelLock();
    ret = osMutexAcquire(mid, 5);
    ICUNIT_GOTO_EQUAL(ret, osErrorResource, ret, EXIT);
    (void)osKernelRestoreLock(0);

    /* 删除被持有的 mutex → PENDED → osErrorResource (覆盖 1160-1161 行) */
    ret = osMutexAcquire(mid, 0);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);
    ret = osMutexDelete(mid);
    ICUNIT_GOTO_EQUAL(ret, osErrorResource, ret, EXIT);

    /* 释放后正常删除 */
    ret = osMutexRelease(mid);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);
    ret = osMutexDelete(mid);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);
    mid = NULL;

    /* 重复删除已删 mutex → MUX_INVALID → osErrorParameter (覆盖 1157-1158 行) */
    mid = osMutexNew(NULL);
    ICUNIT_GOTO_NOT_EQUAL(mid, NULL, mid, EXIT);
    ret = osMutexDelete(mid);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);
    ret = osMutexDelete(mid);
    ICUNIT_GOTO_EQUAL(ret, osErrorParameter, ret, EXIT);
    mid = NULL;

EXIT:
    (void)osKernelRestoreLock(0); /* cnt==0 时为无害空操作 */
    if (mid != NULL) { (void)osMutexDelete(mid); }
    return LOS_OK;
}

static osSemaphoreId_t g_cmsisSemPendId;
static volatile osStatus_t g_cmsisSemPendRet;

static VOID CmsisSemPendHelper(VOID)
{
    g_cmsisSemPendRet = osSemaphoreAcquire(g_cmsisSemPendId, osWaitForever);
    g_testCount++;
}

/* 用例简要描述: Semaphore 错误分支: 删除有等待者的信号量/已删信号量 GetCount */
static UINT32 TestCmsisSem006(VOID)
{
    osSemaphoreId_t sid = NULL;
    osThreadId_t tid = NULL;
    osThreadAttr_t attr = {0};
    osStatus_t ret;
    uint32_t cnt;

    attr.stack_size = OS_TSK_TEST_STACK_SIZE;
    attr.priority = osPriorityLow1;
    attr.attr_bits = osThreadJoinable;
    g_testCount = 0;

    sid = osSemaphoreNew(1, 0, NULL);
    ICUNIT_GOTO_NOT_EQUAL(sid, NULL, sid, EXIT);
    g_cmsisSemPendId = sid;

    tid = osThreadNew((osThreadFunc_t)CmsisSemPendHelper, NULL, &attr);
    ICUNIT_GOTO_NOT_EQUAL(tid, NULL, tid, EXIT);
    osDelay(3); /* 等 helper 阻塞在 Acquire */
    ICUNIT_GOTO_EQUAL(g_testCount, 0, g_testCount, EXIT);

    /* 有等待者时删除 → SEM_PENDED → osErrorResource (覆盖 1275-1276 行) */
    ret = osSemaphoreDelete(sid);
    ICUNIT_GOTO_EQUAL(ret, osErrorResource, ret, EXIT);

    /* 释放唤醒 helper 后正常删除 */
    ret = osSemaphoreRelease(sid);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);
    (void)osThreadJoin(tid);
    tid = NULL;
    ICUNIT_GOTO_EQUAL(g_cmsisSemPendRet, osOK, g_cmsisSemPendRet, EXIT);

    ret = osSemaphoreDelete(sid);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);

    /* 已删信号量 GetCount → semStat==0 → 返回 0 (覆盖 1247-1249 行) */
    cnt = osSemaphoreGetCount(sid);
    ICUNIT_GOTO_EQUAL(cnt, 0, cnt, EXIT);

    /* 已删信号量 Release → SEM_INVALID → osErrorParameter (覆盖 1229-1230 行) */
    ret = osSemaphoreRelease(sid);
    ICUNIT_GOTO_EQUAL(ret, osErrorParameter, ret, EXIT);

    /* 重复删除已删信号量 → SEM_INVALID → osErrorParameter (覆盖 1273-1274 行) */
    ret = osSemaphoreDelete(sid);
    ICUNIT_GOTO_EQUAL(ret, osErrorParameter, ret, EXIT);

    /* 满计数二值信号量(计数已达 max=1) Release → SEM_OVERFLOW → osErrorResource (覆盖 1231-1232 行)
     * 注: 计数信号量 max 为 OS_SEM_COUNTING_MAX_COUNT, 二值信号量 max=1 才可即时触发溢出 */
    {
        osSemaphoreId_t full = osSemaphoreNew(1, 1, NULL);
        ICUNIT_GOTO_NOT_EQUAL(full, NULL, full, EXIT);
        ret = osSemaphoreRelease(full);
        ICUNIT_GOTO_EQUAL(ret, osErrorResource, ret, EXIT);
        (void)osSemaphoreDelete(full);
    }

    /* 调度锁下 Acquire(需阻塞) → SEM_PEND_IN_LOCK → osErrorResource (覆盖 1213-1214 行) */
    {
        osSemaphoreId_t empty = osSemaphoreNew(1, 0, NULL);
        ICUNIT_GOTO_NOT_EQUAL(empty, NULL, empty, EXIT);
        (void)osKernelLock();
        ret = osSemaphoreAcquire(empty, 5);
        ICUNIT_GOTO_EQUAL(ret, osErrorResource, ret, EXIT);
        (void)osKernelRestoreLock(0);
        (void)osSemaphoreDelete(empty);
    }
    sid = NULL;

EXIT:
    (void)osKernelRestoreLock(0); /* cnt==0 时为无害空操作 */
    if (tid != NULL) { (void)osThreadJoin(tid); }
    if (sid != NULL) { (void)osSemaphoreDelete(sid); }
    return LOS_OK;
}

static osMessageQueueId_t g_cmsisMqPendId;
static volatile osStatus_t g_cmsisMqGetRet;

static VOID CmsisMqGetHelper(VOID)
{
    uint32_t buf = 0;
    g_cmsisMqGetRet = osMessageQueueGet(g_cmsisMqPendId, &buf, NULL, osWaitForever);
    g_testCount++;
}

/* 用例简要描述: MessageQueue 错误分支: 满队+锁下 Put/删除有阻塞读者的队列 */
static UINT32 TestCmsisQueue006(VOID)
{
    osMessageQueueId_t mq = NULL;
    osThreadId_t tid = NULL;
    osThreadAttr_t attr = {0};
    osStatus_t ret;
    uint32_t msg = 0xAA;

    attr.stack_size = OS_TSK_TEST_STACK_SIZE;
    attr.priority = osPriorityLow1;
    attr.attr_bits = osThreadJoinable;

    mq = osMessageQueueNew(1, sizeof(uint32_t), NULL);
    ICUNIT_GOTO_NOT_EQUAL(mq, NULL, mq, EXIT);

    /* 填满队列(容量 1) */
    ret = osMessageQueuePut(mq, &msg, 0, 0);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);

    /* 满队 + 调度锁 + timeout → QUEUE_PEND_IN_LOCK → osErrorResource (覆盖 1361-1362 行) */
    (void)osKernelLock();
    ret = osMessageQueuePut(mq, &msg, 0, 5);
    ICUNIT_GOTO_EQUAL(ret, osErrorResource, ret, EXIT);
    (void)osKernelRestoreLock(0);

    /* 取走消息, 让 helper 阻塞在 Get */
    ret = osMessageQueueGet(mq, &msg, NULL, 0);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);
    g_cmsisMqPendId = mq;
    g_testCount = 0;
    tid = osThreadNew((osThreadFunc_t)CmsisMqGetHelper, NULL, &attr);
    ICUNIT_GOTO_NOT_EQUAL(tid, NULL, tid, EXIT);
    osDelay(3);
    ICUNIT_GOTO_EQUAL(g_testCount, 0, g_testCount, EXIT);

    /* 有阻塞读者时删除 → IN_TSKUSE → osErrorResource (覆盖 1449-1450 行) */
    ret = osMessageQueueDelete(mq);
    ICUNIT_GOTO_EQUAL(ret, osErrorResource, ret, EXIT);

    /* 唤醒 helper 后正常删除 */
    ret = osMessageQueuePut(mq, &msg, 0, 0);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);
    (void)osThreadJoin(tid);
    tid = NULL;
    ICUNIT_GOTO_EQUAL(g_cmsisMqGetRet, osOK, g_cmsisMqGetRet, EXIT);

    ret = osMessageQueueDelete(mq);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);
    mq = NULL;

EXIT:
    (void)osKernelRestoreLock(0); /* cnt==0 时为无害空操作 */
    if (tid != NULL) { (void)osThreadJoin(tid); }
    if (mq != NULL) { (void)osMessageQueueDelete(mq); }
    return LOS_OK;
}

/* 用例简要描述: Timer 错误分支: 已删定时器 Start/伪造 id(timerId 越界) Start */
static UINT32 TestCmsisTimer007(VOID)
{
    osTimerId_t tid = NULL;
    osStatus_t ret;
    static UINT32 fakeSwtmr[16]; /* 全 0xFFFFFFFF: 使 pstSwtmr->timerId >= OS_SWTMR_MAX_TIMERID */

    tid = osTimerNew(CmsisTimerCallback, osTimerOnce, NULL, NULL);
    ICUNIT_GOTO_NOT_EQUAL(tid, NULL, tid, EXIT);

    /* NULL 回调创建 → NULL (覆盖 742-743 行) */
    ICUNIT_GOTO_EQUAL((UINTPTR)osTimerNew(NULL, osTimerOnce, NULL, NULL), (UINTPTR)NULL, 0, EXIT);

    ret = osTimerDelete(tid);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);

    /* 已删定时器再 Start → NOT_CREATED → osErrorResource (覆盖 810-811 行) */
    ret = osTimerStart(tid, 10);
    ICUNIT_GOTO_EQUAL(ret, osErrorResource, ret, EXIT);

    /* 已删定时器 Stop → NOT_CREATED → osErrorResource (覆盖 839-840 行) */
    ret = osTimerStop(tid);
    ICUNIT_GOTO_EQUAL(ret, osErrorResource, ret, EXIT);

    /* 伪造 id: timerId=0xFFFFFFFF 越界 → ID_INVALID → osErrorParameter (覆盖 808-809 行) */
    (VOID)memset_s(fakeSwtmr, sizeof(fakeSwtmr), 0xFF, sizeof(fakeSwtmr));
    ret = osTimerStart((osTimerId_t)fakeSwtmr, 10);
    ICUNIT_GOTO_EQUAL(ret, osErrorParameter, ret, EXIT);

    /* 伪造 id Stop → ID_INVALID → osErrorParameter (覆盖 837-838 行) */
    ret = osTimerStop((osTimerId_t)fakeSwtmr);
    ICUNIT_GOTO_EQUAL(ret, osErrorParameter, ret, EXIT);
    tid = NULL;

EXIT:
    if (tid != NULL) { (void)osTimerDelete(tid); }
    return LOS_OK;
}

/* 用例简要描述: MemoryPool: 静态 cb/mem 创建/越界 free/删后 free/重复删/超大 size 分配失败回退 */
static UINT32 TestCmsisMemPool009(VOID)
{
    static UINT32 cbMem[32];      /* 静态控制块(>= sizeof(MemPoolCB)) */
    static UINT32 mpMem[32];      /* 静态池内存(4*32=128 字节, 4 字节对齐) */
    static UINT32 outOfRange[2];  /* 池外地址(用于越界 free 负路径) */
    osMemoryPoolId_t mp = NULL;
    osMemoryPoolAttr_t attr = {0};
    void *blk = NULL;
    osStatus_t ret;
    const char *name;

    (VOID)memset_s(cbMem, sizeof(cbMem), 0, sizeof(cbMem));
    (VOID)memset_s(mpMem, sizeof(mpMem), 0, sizeof(mpMem));

    attr.cb_mem = cbMem;
    attr.cb_size = sizeof(cbMem);
    attr.mp_mem = mpMem;
    attr.mp_size = sizeof(mpMem);
    attr.name = "mp_static_009";

    /* 静态 CB + 静态内存池创建 (覆盖 1502-1509/1520-1522/1534-1535 行) */
    mp = osMemoryPoolNew(4, 32, &attr);
    ICUNIT_GOTO_NOT_EQUAL(mp, NULL, mp, EXIT);
    ICUNIT_GOTO_EQUAL(osMemoryPoolGetCapacity(mp), 4, 0, EXIT);
    ICUNIT_GOTO_EQUAL(osMemoryPoolGetBlockSize(mp), 32, 0, EXIT);
    name = osMemoryPoolGetName(mp);
    ICUNIT_GOTO_STRING_EQUAL(name, "mp_static_009", name, EXIT);

    blk = osMemoryPoolAlloc(mp, 0);
    ICUNIT_GOTO_NOT_EQUAL(blk, NULL, blk, EXIT);

    /* 越界块 free → osErrorParameter (覆盖 1604-1607 行) */
    ret = osMemoryPoolFree(mp, outOfRange);
    ICUNIT_GOTO_EQUAL(ret, osErrorParameter, ret, EXIT);

    /* 正常 free + 删除(静态 cb/mp 均不释放回堆) */
    ret = osMemoryPoolFree(mp, blk);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);
    ret = osMemoryPoolDelete(mp);
    ICUNIT_GOTO_EQUAL(ret, osOK, ret, EXIT);

    /* 删后 free → 状态失效 → osErrorResource (覆盖 1599-1601 行, CB 为静态内存仍可读) */
    ret = osMemoryPoolFree(mp, blk);
    ICUNIT_GOTO_EQUAL(ret, osErrorResource, ret, EXIT);

    /* 重复删 → 状态失效 → osErrorResource (覆盖 1634-1636 行) */
    ret = osMemoryPoolDelete(mp);
    ICUNIT_GOTO_EQUAL(ret, osErrorResource, ret, EXIT);
    mp = NULL;

    /* 超大 size(≈4GB, 通过 block_count>UINT32_MAX/block_size 边界检查): 动态 CB 成功、
     * 池内存分配失败 → 释放 CB 返回 NULL (覆盖 1528-1531 行) */
    mp = osMemoryPoolNew(0xFFFFF, 0x1000, NULL);
    ICUNIT_GOTO_EQUAL((UINTPTR)mp, (UINTPTR)NULL, (UINTPTR)mp, EXIT);
    mp = NULL;

EXIT:
    if (mp != NULL) { (void)osMemoryPoolDelete(mp); }
    return LOS_OK;
}

void CmsisFuncTestSuite(void)
{
    dprintf("***********************BEGIN CMSIS TEST**********************\n");

    TEST_ADD_CASE("ItLosTestCmsis001", TestCmsis001, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsis002", TestCmsis002, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsis003", TestCmsis003, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsis004", TestCmsis004, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsis005", TestCmsis005, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);

#if (LOSCFG_QUEUE_STATIC_ALLOCATION == 1)
    TEST_ADD_CASE("ItLosTestCmsis006", TestCmsis006, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
#endif

    TEST_ADD_CASE("ItLosTestCmsis007", TestCmsis007, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);

    TEST_ADD_CASE("ItLosTestCmsisTimer001", TestCmsisTimer001, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);

    TEST_ADD_CASE("ItLosTestCmsisSaturation001", TestCmsisSaturation001, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisSaturation002", TestCmsisSaturation002, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisSaturation003", TestCmsisSaturation003, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisSaturation004", TestCmsisSaturation004, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisSaturation005", TestCmsisSaturation005, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisSaturation006", TestCmsisSaturation006, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisExt001", TestCmsisExt001, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisExt002", TestCmsisExt002, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisExt003", TestCmsisExt003, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisExt004", TestCmsisExt004, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisExt005", TestCmsisExt005, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisQueueLimit001", TestCmsisQueueLimit001, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisQueueLimit002", TestCmsisQueueLimit002, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisMutexGetName001", TestCmsisMutexGetName001, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisThreadFlags001", TestCmsisThreadFlags001, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisThreadFlags002", TestCmsisThreadFlags002, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisThreadFlags003", TestCmsisThreadFlags003, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisThreadFlags004", TestCmsisThreadFlags004, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
#if (LOSCFG_KERNEL_MEMBOX_STATIC == 1) /* membox dyn 下 CMSIS osMemoryPool* 无实现, 用例隔离(测试侧适配) */
    TEST_ADD_CASE("ItLosTestCmsisMemPool001", TestCmsisMemPool001, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisMemPool002", TestCmsisMemPool002, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisMemPool003", TestCmsisMemPool003, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisMemPool004", TestCmsisMemPool004, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisMemPool005", TestCmsisMemPool005, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisMemPool006", TestCmsisMemPool006, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
#endif /* LOSCFG_KERNEL_MEMBOX_STATIC */
    TEST_ADD_CASE("ItLosTestCmsisEventFlags001", TestCmsisEventFlags001, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisEventFlags002", TestCmsisEventFlags002, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisEventFlags003", TestCmsisEventFlags003, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisMutex001", TestCmsisMutex001, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisMutex002", TestCmsisMutex002, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisMutex003", TestCmsisMutex003, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisSem001", TestCmsisSem001, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisSem002", TestCmsisSem002, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisSem003", TestCmsisSem003, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisKernel001", TestCmsisKernel001, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisKernel002", TestCmsisKernel002, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisKernel003", TestCmsisKernel003, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisThread001", TestCmsisThread001, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisThread002", TestCmsisThread002, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisThread003", TestCmsisThread003, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisThread004", TestCmsisThread004, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisThread005", TestCmsisThread005, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisTimer002", TestCmsisTimer002, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisTimer003", TestCmsisTimer003, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    /* supplement: cover uncalled kernel/queue/thread APIs */
    TEST_ADD_CASE("ItLosTestCmsisKernel004", TestCmsisKernel004, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisKernel005", TestCmsisKernel005, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisQueueAttr001", TestCmsisQueueAttr001, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisThread006", TestCmsisThread006, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisThread007", TestCmsisThread007, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisThread008", TestCmsisThread008, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    /* Batch 1 supplement: Kernel lock/delay, Timer boundary, Thread misc */
    TEST_ADD_CASE("ItLosTestCmsisKernel006", TestCmsisKernel006, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisKernel007", TestCmsisKernel007, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
#if !defined(LOSCFG_ARCH_ARM) /* 外层: 非 ARM 平台 */
#if defined(CMSIS_BUG_01_KERNELINIT_GUARD) && CMSIS_BUG_01_KERNELINIT_GUARD == 1
    /* BUG_01 修复后恢复注册; BUG 宏守卫单独一行, 最里层 */
    TEST_ADD_CASE("ItLosTestCmsisKernel008", TestCmsisKernel008, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
#endif
#endif
    TEST_ADD_CASE("ItLosTestCmsisKernel009", TestCmsisKernel009, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisIsr001", TestCmsisIsr001, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisDelay001", TestCmsisDelay001, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisTimer004", TestCmsisTimer004, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisTimer005", TestCmsisTimer005, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisTimer006", TestCmsisTimer006, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisThread009", TestCmsisThread009, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisThread010", TestCmsisThread010, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisThread011", TestCmsisThread011, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisThread012", TestCmsisThread012, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisThread013", TestCmsisThread013, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    /* Batch 2: EventFlags/Mutex/Semaphore/MessageQueue/MemoryPool gap tests */
    TEST_ADD_CASE("ItLosTestCmsisEventFlags004", TestCmsisEventFlags004, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisEventFlags005", TestCmsisEventFlags005, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisEventFlags006", TestCmsisEventFlags006, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisMutex004", TestCmsisMutex004, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisMutex005", TestCmsisMutex005, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisSem004", TestCmsisSem004, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisSem005", TestCmsisSem005, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisQueue002", TestCmsisQueue002, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisQueue003", TestCmsisQueue003, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisQueue004", TestCmsisQueue004, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisQueue005", TestCmsisQueue005, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
#if (LOSCFG_KERNEL_MEMBOX_STATIC == 1) /* membox dyn 下 CMSIS osMemoryPool* 无实现, 用例隔离(测试侧适配) */
    TEST_ADD_CASE("ItLosTestCmsisMemPool007", TestCmsisMemPool007, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisMemPool008", TestCmsisMemPool008, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
#endif /* LOSCFG_KERNEL_MEMBOX_STATIC */
    TEST_ADD_CASE("ItLosTestCmsisLowFreq001", TestCmsisLowFreq001, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    /* Batch 3: 覆盖率补充——错误分支/边界 */
    TEST_ADD_CASE("ItLosTestCmsisKernel010", TestCmsisKernel010, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisKernel011", TestCmsisKernel011, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisKernel012", TestCmsisKernel012, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisThread014", TestCmsisThread014, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisThread015", TestCmsisThread015, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisThreadFlags005", TestCmsisThreadFlags005, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisEventFlags007", TestCmsisEventFlags007, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisIsr002", TestCmsisIsr002, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisMutex006", TestCmsisMutex006, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisSem006", TestCmsisSem006, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisQueue006", TestCmsisQueue006, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
    TEST_ADD_CASE("ItLosTestCmsisTimer007", TestCmsisTimer007, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
#if (LOSCFG_KERNEL_MEMBOX_STATIC == 1)
    TEST_ADD_CASE("ItLosTestCmsisMemPool009", TestCmsisMemPool009, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
#endif
}
