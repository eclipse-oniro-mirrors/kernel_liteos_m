/*
 * Copyright (c) 2023-2023 Huawei Device Co., Ltd. All rights reserved.
 * Copyright (c) 2021 Nuclei Limited. All rights reserved.
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

#include <stdio.h>
#include <stdarg.h>
#include "los_arch.h"
#include "los_arch_interrupt.h"
#include "los_arch_context.h"
#include "los_task.h"
#include "los_debug.h"
#include "nuclei_sdk_hal.h"

STATIC UINT32 HwiUnmask(HWI_HANDLE_T hwiNum)
{
    if (hwiNum >= OS_HWI_MAX_NUM) {
        return OS_ERRNO_HWI_NUM_INVALID;
    }

    ECLIC_EnableIRQ(hwiNum);

    return LOS_OK;
}

STATIC UINT32 HwiMask(HWI_HANDLE_T hwiNum)
{
    if (hwiNum >= OS_HWI_MAX_NUM) {
        return OS_ERRNO_HWI_NUM_INVALID;
    }

    ECLIC_DisableIRQ(hwiNum);

    return LOS_OK;
}

STATIC UINT32 HwiSetPriority(HWI_HANDLE_T hwiNum, UINT8 priority)
{
    if (hwiNum >= OS_HWI_MAX_NUM) {
        return OS_ERRNO_HWI_NUM_INVALID;
    }

    if (priority > OS_HWI_PRIO_HIGHEST || priority < OS_HWI_PRIO_LOWEST) {
        return OS_ERRNO_HWI_PRIO_INVALID;
    }

    ECLIC_SetPriorityIRQ(hwiNum, (hwiPrio & 0xffff));

    return LOS_OK;
}

LITE_OS_SEC_TEXT_INIT VOID HalHwiInit(VOID)
{
    // already setup interrupt vectors
}

/*****************************************************************************
 Function    : ArchHwiCreate
 Description : create hardware interrupt
 Input       : hwiNum       --- hwi num to create
               hwiPrio      --- priority of the hwi
               hwiMode      --- hwi interrupt hwiMode, between vector or non-vector
               hwiHandler   --- hwi handler
               irqParam     --- set trig hwiMode of the hwi handler
                                Level Triggerred = 0
                                Postive/Rising Edge Triggered = 1
                                Negtive/Falling Edge Triggered = 3
 Output      : None
 Return      : LOS_OK on success or error code on failure
 *****************************************************************************/
UINT32 ArchHwiCreate(HWI_HANDLE_T hwiNum,
                     HWI_PRIOR_T hwiPrio,
                     HWI_MODE_T hwiMode,
                     HWI_PROC_FUNC hwiHandler,
                     HwiIrqParam *irqParam)
{
    if (hwiNum > SOC_INT_MAX) {
        return OS_ERRNO_HWI_NUM_INVALID;
    }
    if (hwiMode > ECLIC_VECTOR_INTERRUPT) {
        return OS_ERRNO_HWI_MODE_INVALID;
    }
    if ((irqParam == NULL) || (irqParam->pDevId > ECLIC_NEGTIVE_EDGE_TRIGGER)) {
        return OS_ERRNO_HWI_ARG_INVALID;
    }

    /* set interrupt vector hwiMode */
    ECLIC_SetShvIRQ(hwiNum, hwiMode);
    /* set interrupt trigger hwiMode and polarity */
    ECLIC_SetTrigIRQ(hwiNum, irqParam->pDevId);
    /* set interrupt level */
    // default to 0
    ECLIC_SetLevelIRQ(hwiNum, 0);
    /* set interrupt priority */
    // high 16 bit for level
    ECLIC_SetLevelIRQ(hwiNum, (hwiPrio >> 16));
    /* set interrupt priority */
    // low 16 bit for priority
    ECLIC_SetPriorityIRQ(hwiNum, (hwiPrio & 0xffff));
    if (hwiHandler != NULL) {
        /* set interrupt handler entry to vector table */
        ECLIC_SetVector(hwiNum, (rv_csr_t)hwiHandler);
    }
    /* enable interrupt */
    HwiUnmask(hwiNum);
    return LOS_OK;
}

/*****************************************************************************
 Function    : ArchHwiDelete
 Description : Delete hardware interrupt
 Input       : hwiNum   --- hwi num to delete
               irqParam --- param of the hwi handler
 Return      : LOS_OK on success or error code on failure
 *****************************************************************************/
LITE_OS_SEC_TEXT UINT32 ArchHwiDelete(HWI_HANDLE_T hwiNum, HwiIrqParam *irqParam)
{
    (VOID)irqParam;
    // change func to default func
    ECLIC_SetVector(hwiNum, (rv_csr_t)HalHwiDefaultHandler);
    // disable interrupt
    HwiMask(hwiNum);
    return LOS_OK;
}

/* ****************************************************************************
 Function    : HalDisplayTaskInfo
 Description : display the task list
 Input       : None
 Output      : None
 Return      : None
 **************************************************************************** */
VOID HalDisplayTaskInfo(VOID)
{
    TSK_INFO_S taskInfo;
    UINT32 index;
    UINT32 ret;

    PRINTK("ID  Pri    Status     name \r\n");
    PRINTK("--  ---    ---------  ----\r\n");

    for (index = 0; index < LOSCFG_BASE_CORE_TSK_LIMIT; index++) {
        ret = LOS_TaskInfoGet(index, &taskInfo);
        if (ret != LOS_OK) {
            continue;
        }
        PRINTK("%d    %d     %s      %s \r\n",
               taskInfo.uwTaskID, taskInfo.usTaskPrio, OsConvertTskStatus(taskInfo.usTaskStatus), taskInfo.acName);
    }
    return;
}

/* ****************************************************************************
 Function    : HalUnalignedAccessFix
 Description : Unaligned access fixes are not supported by default
 Input       : None
 Output      : None
 Return      : None
 **************************************************************************** */
WEAK UINT32 HalUnalignedAccessFix(UINTPTR mcause, UINTPTR mepc, UINTPTR mtval, VOID *sp)
{
    /* Unaligned access fixes are not supported by default */
    PRINTK("Unaligned access fixes are not support by default!\r\n");
    return LOS_NOK;
}

__attribute__((always_inline)) inline VOID HalIntEnter(VOID)
{
    g_intCount += 1;
}

__attribute__((always_inline)) inline VOID HalIntExit(VOID)
{
    g_intCount -= 1;
}

STATIC HwiControllerOps g_archHwiOps = {
    .enableIrq      = HwiUnmask,
    .disableIrq     = HwiMask,
    .setIrqPriority = HwiSetPriority,
};

HwiControllerOps *ArchIntOpsGet(VOID)
{
    return &g_archHwiOps;
}
