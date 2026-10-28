/*
 * Copyright (c) 2026-2026 Huawei Device Co., Ltd. All rights reserved.
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

#include "los_runstop_pri.h"
#include "los_event.h"
#include "los_pm.h"
#include "los_interrupt.h"
#include "los_hwi_pri.h"
#include "los_debug.h"
#include "los_memory.h"
#include "los_common_interrupt.h"
#include "los_arch_pm.h"
#ifdef LOSCFG_KERNEL_MEM_BESTFIT
#include "los_memory_pri.h"
#endif

#define WAKEUP_FROM_SUSPEND 0x01
#define FLASH_IMG_SUCCESS   0x02
#define FLASH_IMG_FAIL      0x04
/* Event bit signaling OsWriteToFlashTask to snapshot the system. */
#define WRITE_FLASH_TRIGGER 0x01
#define SUSPEND_EVENT_ALL (WAKEUP_FROM_SUSPEND | FLASH_IMG_SUCCESS | FLASH_IMG_FAIL)

extern CHAR _sdata;
extern CHAR _ram_save_end[];

/* If core is ready for imaging */
LITE_OS_SEC_DATA_MINOR STATIC UINT32 g_sysDoneFlag = OS_NO_STORE_SYSTEM;

/* Start position of flash to write image */
LITE_OS_SEC_DATA_MINOR STATIC UINTPTR g_flashImgAddr;

/*
 * Staging address for the heap after image carry.
 * Moved to the real heap address on resume.
 */
LITE_OS_SEC_DATA_MINOR STATIC const VOID *g_heapMemStart = NULL;

/*
 * Raw heap watermark (not page-rounded).
 * Consumed by the end-node rebuild on resume.
 */
LITE_OS_SEC_DATA_MINOR STATIC size_t g_heapMemSize = 0;

/* Size of wow image */
LITE_OS_SEC_DATA_MINOR STATIC size_t g_wowImgSize = 0;

LITE_OS_SEC_DATA_MINOR STATIC RUNSTOP_PARAM_S g_runstopParam;
LITE_OS_SEC_DATA_MINOR STATIC EVENT_CB_S g_suspendResumeEvent;
LITE_OS_SEC_DATA_MINOR STATIC EVENT_CB_S g_writeFlashEvent;

typedef struct {
    UINTPTR memStart;
    UINTPTR flashStart;
    size_t memSize;
} FlashWriteParam;

LITE_OS_SEC_DATA_MINOR LosTaskCB *g_saveTsk = NULL;

STATIC BOOL g_makeImageActive = FALSE;
STATIC BOOL g_runstopInited = FALSE;

BOOL IsImageResume(VOID)
{
    return (g_resumeFromImg != LOS_COLD_RESET);
}

LITE_OS_SEC_TEXT_MINOR STATIC INT32 OsDoWriteWow2Flash(FLASH_WRITE_FUNC flashWriteFunc,
                                                      const FlashWriteParam *fixedSection,
                                                      const FlashWriteParam *heapMemSection)
{
    if ((fixedSection->memSize != 0) &&
        (flashWriteFunc((VOID *)fixedSection->memStart, fixedSection->flashStart,
                        fixedSection->memSize) != 0)) {
        PRINT_ERR("%s, %d\n", __FUNCTION__, __LINE__);
        return -1;
    }

    if ((heapMemSection->memSize != 0) &&
        (flashWriteFunc((VOID *)heapMemSection->memStart, heapMemSection->flashStart,
                        heapMemSection->memSize) != 0)) {
        PRINT_ERR("%s, %d\n", __FUNCTION__, __LINE__);
        return -1;
    }
    return 0;
}

LITE_OS_SEC_TEXT_MINOR INT32 OsWriteWow2Flash(VOID)
{
    FlashWriteParam fixedSection;
    FlashWriteParam heapMemSection;
    size_t eraseAlignSize;
    size_t writeAlignSize;
    UINTPTR waterMark;
    FLASH_WRITE_FUNC flashWriteFunc = g_runstopParam.pfFlashWriteFunc;

    eraseAlignSize = g_runstopParam.uwFlashEraseAlignSize;
    writeAlignSize = g_runstopParam.uwFlashWriteAlignSize;
    writeAlignSize = (writeAlignSize >= eraseAlignSize) ? writeAlignSize : eraseAlignSize;
    if (flashWriteFunc == NULL) {
        PRINT_ERR("%s, %d\n", __FUNCTION__, __LINE__);
        return -1;
    }

    fixedSection.memStart = (UINTPTR)&_sdata;
    fixedSection.flashStart = g_flashImgAddr;
    fixedSection.memSize = (UINTPTR)&_ram_save_end - (UINTPTR)&_sdata;
    /*
     * Heap section must start at the next erase boundary.
     */
    fixedSection.memSize = (fixedSection.memSize + writeAlignSize - 1) & ~(writeAlignSize - 1);

#ifdef LOSCFG_KERNEL_MEM_BESTFIT
    waterMark = LOS_MemLastUsedGet((VOID *)m_aucSysMem1);
    if ((waterMark < (UINTPTR)m_aucSysMem1) || (waterMark > (UINTPTR)&_heap_end)) {
        waterMark = (waterMark < (UINTPTR)m_aucSysMem1) ? (UINTPTR)m_aucSysMem1 : (UINTPTR)&_heap_end;
    }
#else
    waterMark = (UINTPTR)&_heap_end;
#endif
    heapMemSection.memStart = (UINTPTR)m_aucSysMem1;
    heapMemSection.flashStart = g_flashImgAddr + fixedSection.memSize;
    heapMemSection.memSize = waterMark - heapMemSection.memStart;

    g_heapMemSize = heapMemSection.memSize;
    g_heapMemStart = (const VOID *)&_ram_save_end;
    g_wowImgSize = fixedSection.memSize + heapMemSection.memSize;

    return OsDoWriteWow2Flash(flashWriteFunc, &fixedSection, &heapMemSection);
}

LITE_OS_SEC_TEXT_MINOR VOID OsSystemSuspend(VOID)
{
    (VOID)LOS_IntLock();
    LOS_TaskLock();

    g_sysDoneFlag = OS_NO_STORE_SYSTEM;
    g_saveTsk = OsCurrTaskGet();

    OsSRSaveRegister();

    if (g_resumeFromImg) {
        INT32 flashRet = OsWriteWow2Flash();
        LOS_TaskUnlock();
        (VOID)LOS_IntUnLock();
        (VOID)LOS_EventWrite(&g_suspendResumeEvent,
                             (flashRet == 0) ? FLASH_IMG_SUCCESS : FLASH_IMG_FAIL);
    } else {
        if ((g_pmCB.tickTimer != NULL) && (g_pmCB.tickTimer->tickUnlock != NULL)) {
            g_pmCB.tickTimer->tickUnlock();
        }
        LOS_TaskUnlock();
        (VOID)LOS_IntUnLock();

        if (g_runstopParam.pfIdleWakeupCallback != NULL) {
            g_runstopParam.pfIdleWakeupCallback();
        }
        (VOID)LOS_EventWrite(&g_suspendResumeEvent, WAKEUP_FROM_SUSPEND);
    }
}

LITE_OS_SEC_TEXT VOID OsWriteToFlashTask(VOID)
{
    (VOID)LOS_EventInit(&g_writeFlashEvent);

    while (1) {
        (VOID)LOS_EventRead(&g_writeFlashEvent, WRITE_FLASH_TRIGGER, LOS_WAITMODE_OR | LOS_WAITMODE_CLR, LOS_WAIT_FOREVER);
        OsSystemSuspend();
    }
}

LITE_OS_SEC_TEXT VOID OsStoreSystemInfoBeforeSuspend(VOID)
{
    if (OsWowSysDoneFlagGet() != OS_STORE_SYSTEM) {
        return;
    }
    (VOID)LOS_EventWrite(&g_writeFlashEvent, WRITE_FLASH_TRIGGER);
}

LITE_OS_SEC_TEXT_MINOR VOID OsSystemWakeup(VOID)
{
    if (!g_resumeFromImg) {
        return;
    }

    /*
     * Relocate heap from staging to real address.
     * Descending copy when dst > src (overlap safe).
     */
    if (g_heapMemStart != (const VOID *)m_aucSysMem1) {
        UINT32 i;
        volatile UINT32 *dst = (volatile UINT32 *)m_aucSysMem1;
        const volatile UINT32 *src = (const volatile UINT32 *)g_heapMemStart;
        UINT32 words = (UINT32)(g_heapMemSize / sizeof(UINT32));
        if (dst < src) {
            for (i = 0; i < words; i++) {
                dst[i] = src[i];
            }
        } else {
            for (i = words; i > 0; i--) {
                dst[i - 1] = src[i - 1];
            }
        }
    }
    dsb();

    /*
     * Set the task of the current CPU.
     */
    OsCurrTaskSet(g_saveTsk);

#ifdef LOSCFG_KERNEL_MEM_BESTFIT
    /*
     * Rebuild the pool end node at the truncation boundary.
     */
    if (LOS_MemPoolSizeGet((VOID *)m_aucSysMem1) != g_heapMemSize) {
        OsMemResetEndNode((VOID *)m_aucSysMem1, (UINTPTR)m_aucSysMem1 + g_heapMemSize);
    }
#endif

    OsResumeInterruptSystem();

    g_resumeFromImg = LOS_COLD_RESET;

    OsSRRestoreRegister();
}

LITE_OS_SEC_TEXT_MINOR UINT32 OsWaitImagingDone(UINTPTR wowFlashAddr, size_t *wowImgSize)
{
    UINT32 ret;

    g_flashImgAddr = wowFlashAddr;
    (VOID)LOS_EventInit(&g_suspendResumeEvent);

    /* This flag travels in the snapshot and drives the wake path. */
    g_resumeFromImg = LOS_RUN_STOP_RESET;
    /* Drives the suspend path; reset on wake. */
    g_sysDoneFlag = OS_STORE_SYSTEM;

    ret = LOS_EventRead(&g_suspendResumeEvent, SUSPEND_EVENT_ALL, LOS_WAITMODE_OR | LOS_WAITMODE_CLR, LOS_WAIT_FOREVER);
    if (wowImgSize != NULL) {
        *wowImgSize = g_wowImgSize;
    }

    return ret;
}

LITE_OS_SEC_TEXT_MINOR VOID OsRunstopParamInit(const RUNSTOP_PARAM_S *runstopParam)
{
    g_runstopParam.pfIdleWakeupCallback = runstopParam->pfIdleWakeupCallback;
    g_runstopParam.pfWakeupCallback = runstopParam->pfWakeupCallback;
    g_runstopParam.pfImageDoneCallback = runstopParam->pfImageDoneCallback;
    g_runstopParam.pfErrorCallback = runstopParam->pfErrorCallback;
    g_runstopParam.pfFlashReadFunc = runstopParam->pfFlashReadFunc;
    g_runstopParam.pfFlashWriteFunc = runstopParam->pfFlashWriteFunc;
    g_runstopParam.uwFlashReadAlignSize = runstopParam->uwFlashReadAlignSize;
    g_runstopParam.uwFlashEraseAlignSize = runstopParam->uwFlashEraseAlignSize;
    g_runstopParam.uwFlashWriteAlignSize = runstopParam->uwFlashWriteAlignSize;
    g_runstopParam.uwWowFlashAddr = runstopParam->uwWowFlashAddr;
}

LITE_OS_SEC_TEXT_MINOR VOID LOS_MakeImage(const RUNSTOP_PARAM_S *runstopParam)
{
    UINT32 ret;
    size_t imgSize;

    if ((runstopParam == NULL) || g_makeImageActive) {
        return;
    }
    g_makeImageActive = TRUE;

    OsRunstopParamInit(runstopParam);

    if (LOS_PmModeSet(LOS_SYS_RUNSTOP) != LOS_OK) {
        g_makeImageActive = FALSE;
        return;
    }
    if (!g_runstopInited) {
        if (OsWowWriteFlashTaskCreate() != LOS_OK) {
            g_makeImageActive = FALSE;
            return;
        }
        g_runstopInited = TRUE;
    }

    ret = OsWaitImagingDone(g_runstopParam.uwWowFlashAddr, &imgSize);
    if (ret == (UINT32)WAKEUP_FROM_SUSPEND) {
        if (g_runstopParam.pfWakeupCallback != NULL) {
            g_runstopParam.pfWakeupCallback();
        }
        PRINT_INFO("Resume ok!\n");
    } else if (ret == (UINT32)FLASH_IMG_SUCCESS) {
        if (g_runstopParam.pfImageDoneCallback != NULL) {
            g_runstopParam.pfImageDoneCallback();
        }

        PRINT_INFO("Flash ok! Image length 0x%x\n", imgSize);
    } else if (ret == (UINT32)FLASH_IMG_FAIL) {
        if (g_runstopParam.pfErrorCallback != NULL) {
            g_runstopParam.pfErrorCallback();
        }
    }

    g_makeImageActive = FALSE;
}

LITE_OS_SEC_TEXT_INIT_LEVEL_ONE UINT32 OsWowWriteFlashTaskCreate(VOID)
{
    UINT32 ret;
    UINT32 writeFlashTaskId;
    TSK_INIT_PARAM_S taskInitParam = {0};

    taskInitParam.pfnTaskEntry = (TSK_ENTRY_FUNC)OsWriteToFlashTask;
    taskInitParam.uwStackSize = LOSCFG_BASE_CORE_TSK_DEFAULT_STACK_SIZE;
    taskInitParam.pcName = "WowWriteFlashTask";
    taskInitParam.usTaskPrio = LOS_TASK_PRIORITY_LOWEST - 1;
    ret = LOS_TaskCreate(&writeFlashTaskId, &taskInitParam);
    return ret;
}

LITE_OS_SEC_TEXT_MINOR size_t OsWowImageSizeGet(VOID)
{
    return g_wowImgSize;
}

LITE_OS_SEC_TEXT_MINOR UINT32 OsWowSysDoneFlagGet(VOID)
{
    return g_sysDoneFlag;
}
