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
 * 3. Neither the name of the copyright holder nor any of its contributors may be used to
 *    endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */

#include "osTest.h"
#include "It_los_hpl.h"
#include "los_task_pri.h"
#include "los_hpl_logger_pri.h"

/* 用例简要描述: HPL接口非法入参极值与错误码矩阵测试——SizeSet 超大与负数语义 bufSize（0xFFFFFFFF/0x80000000/0x7FFFFFFF）拒绝且无残留、最小尺寸 1 隐式对齐可创建、taskId 极值与首越界边界拒绝、校验顺序探针（ID 先于尺寸/出参先于 ID）、RecordCustomString 超大 bufSize 分配失败返回 LOS_NOK */

STATIC volatile UINT32 g_gate44;
STATIC CHAR g_str44[] = "extreme044";

/* Parked target task: stays alive while the test task drives the parameter
 * matrix against its id, exits (DETACHED auto-delete) on gate release so the
 * staging buffer created by the minimal-size call is freed via preDelete. */
static VOID TaskF44(VOID)
{
    while (g_gate44 == 0) {
        LOS_TaskDelay(1);
    }
}

static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT32 peak;
    BOOL ovf;
    TSK_INIT_PARAM_S taskParam = {0};

    /* Install the record hook first: the periodic compression cycle needs it
     * to write the boot checkpoint; without the hook OsHplDoCompressLog bails
     * out at the checkpoint write and never reaches the handle scan that
     * frees preDelete'd staging buffers. */
    TestHplPrepare();

    g_gate44 = 0;
    taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)TaskF44;
    taskParam.uwStackSize = TASK_STACK_SIZE_TEST;
    taskParam.pcName = "HplExt044";
    taskParam.usTaskPrio = TASK_PRIO_TEST - 1;
    taskParam.uwResved = LOS_TASK_STATUS_DETACHED;
    ret = LOS_TaskCreate(&g_testTaskID01, &taskParam);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* 1. bufSize extreme values on a valid live task. UINT32 has no true
     * negative: the "negative" semantics are the high-bit-set values.
     * LOS_Align is wraparound-aware (los_compiler.h:79-82), so 0xFFFFFFFF
     * aligns to 0xFFFFFFFC instead of wrapping to a small value, and the
     * multi-gigabyte allocation fails deterministically. */
    ret = LOS_TaskHplSizeSet(g_testTaskID01, 0xFFFFFFFFU); /* (UINT32)-1 */
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_HPL_NO_MEMORY, ret);
    ret = LOS_TaskHplSizeSet(g_testTaskID01, 0x80000000U); /* 2GB */
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_HPL_NO_MEMORY, ret);
    ret = LOS_TaskHplSizeSet(g_testTaskID01, 0x7FFFFFFFU); /* INT32_MAX, aligns up to 2GB */
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_HPL_NO_MEMORY, ret);

    /* 2. The three failures leave no half-initialised residue, and the
     * minimal size succeeds: LOS_Align(1, 4) = 4 (implicit alignment). */
    ret = LOS_TaskHplSizeSet(g_testTaskID01, 1);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_TaskHplPeakSizeGet(g_testTaskID01, &peak, &ovf);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ICUNIT_ASSERT_EQUAL(peak, 0, peak);
    ICUNIT_ASSERT_EQUAL(ovf, FALSE, ovf);
    ret = LOS_TaskHplSizeSet(g_testTaskID01, HPL_TEST_STAGING_SIZE); /* repeat on created */
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_HPL_HAS_CREATED, ret);

    /* 3. taskId extremes: OS_TSK_GET_INDEX is the identity macro, so any id
     * >= g_taskMaxNum is rejected before the TCB is dereferenced. The first
     * out-of-range id is read from the kernel at runtime: g_taskMaxNum is
     * KERNEL_TSK_LIMIT (LOSCFG_BASE_CORE_TSK_LIMIT + 1, idle included), so
     * the compile-time macro alone under-reports the boundary. */
    ret = LOS_TaskHplSizeSet(0xFFFFFFFFU, HPL_TEST_STAGING_SIZE);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_TSK_ID_INVALID, ret);
    ret = LOS_TaskHplSizeSet(0x80000000U, HPL_TEST_STAGING_SIZE);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_TSK_ID_INVALID, ret);
    ret = LOS_TaskHplSizeSet(g_taskMaxNum, HPL_TEST_STAGING_SIZE);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_TSK_ID_INVALID, ret); /* first out-of-range id */
    ret = LOS_TaskHplPeakSizeGet(0xFFFFFFFFU, &peak, &ovf);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_TSK_ID_INVALID, ret);
    ret = LOS_TaskHplPeakSizeGet(g_taskMaxNum, &peak, &ovf);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_TSK_ID_INVALID, ret);

    /* 4. Validation-order probes: the task-id check precedes the size check
     * (OsHplTaskIdVerify runs before OsHplCreate), and the out-param check
     * precedes the id check (PeakSizeGet checks pointers first). */
    ret = LOS_TaskHplSizeSet(0xFFFFFFFFU, 0);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_TSK_ID_INVALID, ret);
    ret = LOS_TaskHplPeakSizeGet(0xFFFFFFFFU, NULL, NULL);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_HPL_PARAM_INVALID, ret);

    /* 5. RecordCustomString with huge bufSize: the dynamic allocation of
     * bufSize + sizeof(header) fails deterministically and returns LOS_NOK
     * before any copy happens. (The wraparound zone bufSize >= 0xFFFFFFF0
     * underflows the memcpy_s destMax and stays registered as untestable.) */
    ret = LOS_HplRecordCustomString(g_str44, 0x7FFFFFFFU);
    ICUNIT_ASSERT_EQUAL(ret, LOS_NOK, ret);
    ret = LOS_HplRecordCustomString(g_str44, 0x80000000U);
    ICUNIT_ASSERT_EQUAL(ret, LOS_NOK, ret);

    /* 6. Release the parked task and wait until the compression task frees
     * its minimal staging buffer: OsHplPreDelete (mark) runs in the idle
     * recycle path and OsHplDelete (free) in the next compression cycle.
     * The task never logged, so no delivery cycle synchronises the compress
     * phase; polling the restored hpl handle turns the asynchronous free
     * into a deterministic assertion instead of a fixed drain window. */
    g_gate44 = 1;
    {
        HplHandle *hpl = NULL;
        UINT32 wait = 0;
        do {
            LOS_TaskDelay(1);
            hpl = (HplHandle *)OS_TCB_FROM_TID(g_testTaskID01)->hpl;
            wait++;
        } while ((hpl != NULL) && (hpl->inited == TRUE) && (wait < (HPL_TEST_INT_DRAIN_TICKS * 2)));
        ICUNIT_ASSERT_EQUAL((hpl != NULL) && (hpl->inited == FALSE), 1, wait);
    }
    return LOS_OK;
}

VOID ItLosHpl044(VOID)
{
    TEST_ADD_CASE("ItLosHpl044", TestCase, TEST_LOS, TEST_FN_KERNELEXT_HPL, TEST_LEVEL0, TEST_FUNCTION);
}
