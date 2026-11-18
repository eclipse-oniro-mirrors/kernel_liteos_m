/*
 * los_box_mpu.c — static MPU layout + per-box dynamic region switch.
 * All constants come from asm/memmap_config.h (single source with the
 * linker scripts). Runs privileged; called from main (init) and from the
 * dispatch asm (context switch).
 */
#include "los_typedef.h"
#include "soc.h"                       /* core_cm4.h: MPU, SCB, __DSB/__ISB */
#include "los_common_interrupt.h"      /* ArchGetHwiFrom */
#include "los_box_pri.h"
#include "asm/memmap_config.h"        /* slot geometry + RASR recipes (board) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define OS_SYS_VECTOR_CNT      16
#define SVC_VECTOR_NUM         11     /* g_hwiForm[SVCall_IRQn + 16] */

/* External syscall entry (box/los_svc_entry.S) */
extern VOID OsExcSvcCall(VOID);

/* The box R3/R4 are currently programmed for; NULL = disabled (kernel map).
 *
 * Comparing against THIS rather than against the outgoing task's boxCB is what
 * makes a box -> kernel -> same-box round trip free. The dispatch asm
 * (BOX_PENDSV_RESTORE) skips BoxMpuSwitch when the incoming task is a kernel
 * task, so R3/R4 keep describing the previous box - correct, because kernel
 * tasks run privileged and the box regions only constrain user access, and
 * g_mpuBoxCB therefore still records the truth. Without this guard the way
 * back into that same box would rewrite six MPU registers plus a DSB/ISB pair
 * for nothing.
 *
 * The asm keeps its own new-vs-old boxCB comparison as a cheap fast path (it
 * avoids the call entirely for same-box task switches); this guard catches the
 * cases that comparison cannot see. A box's layout[0] addresses come from the
 * static config and do not move across a destroy/restart, so a matching
 * pointer really does mean a matching mapping (no dynamic boxes in this build:
 * dynboxWord is 0 for both). */
STATIC const LosBoxCB *g_mpuBoxCB = NULL;

VOID BoxMpuSwitch(const LosBoxCB *boxCB)
{
    if (boxCB == g_mpuBoxCB) {
        return;   /* R3/R4 already describe this box (or are already off) */
    }
    g_mpuBoxCB = boxCB;

    if (boxCB == NULL) {
        /* Kernel task: no box regions needed, the background map covers. */
        MPU->RNR = MPU_REGION_R3_BOX_CODE;
        MPU->RASR = 0;                              /* disable */
        MPU->RNR = MPU_REGION_R4_BOX_DATA;
        MPU->RASR = 0;
    } else {
        MPU->RNR = MPU_REGION_R3_BOX_CODE;
        MPU->RBAR = boxCB->layout[0].boxTextStartAddr;
        MPU->RASR = MPU_RASR_BOX_CODE;
        MPU->RNR = MPU_REGION_R4_BOX_DATA;
        MPU->RBAR = boxCB->layout[0].boxDataStartAddr;
        MPU->RASR = MPU_RASR_BOX_DATA;
    }
    __DSB();
    __ISB();
}

VOID MpuLayoutInit(VOID)
{
    HWI_PROC_FUNC *hwiForm = NULL;

    MPU->CTRL = 0;                                  /* configure disabled */

    /* R0: common code, executable from kernel and user boxes. */
    MPU->RNR = MPU_REGION_R0_COMMON_TEXT;
    MPU->RBAR = COMMON_FLASH_BASE;
    MPU->RASR = MPU_RASR_COMMON_TEXT;

    /* R1: common data, RW from both worlds. */
    MPU->RNR = MPU_REGION_R1_COMMON_DATA;
    MPU->RBAR = COMMON_RAM_BASE;
    MPU->RASR = MPU_RASR_COMMON_DATA;

    /* R2: user read-only data, written by kernel only. */
    MPU->RNR = MPU_REGION_R2_USER_READONLY;
    MPU->RBAR = USER_READONLY_RAM_BASE;
    MPU->RASR = MPU_RASR_USER_READONLY;

    /* R3/R4: box slots, disabled until a box task is dispatched. */
    MPU->RNR = MPU_REGION_R3_BOX_CODE;
    MPU->RASR = 0;
    MPU->RNR = MPU_REGION_R4_BOX_DATA;
    MPU->RASR = 0;

    __DSB();
    __ISB();
    MPU->CTRL = MPU_CTRL_ENABLE_PRIVDEF;            /* ENABLE | PRIVDEFENA */

    /* Independent MemManage vector for user-mode violation analysis. */
    SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk;

    /* Take over the SVC vector: HalHwiInit (inside LOS_KernelInit) installed
     * HalExcSvcCall; syscalls own it from now on. */
    hwiForm = (HWI_PROC_FUNC *)ArchGetHwiFrom();
    hwiForm[SVC_VECTOR_NUM] = (HWI_PROC_FUNC)OsExcSvcCall;
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
