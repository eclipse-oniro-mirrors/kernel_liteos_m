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

/* ---- region programming ----
 * RNR selects the region, then RBAR/RASR describe it. RASR bit 0 is ENABLE,
 * so writing 0 retires the region; a retired region's base is meaningless,
 * which is why the disable form leaves RBAR alone. */
STATIC INLINE VOID MpuRegionSet(UINT32 region, UINTPTR baseAddr, UINT32 rasr)
{
    MPU->RNR = region;
    MPU->RBAR = baseAddr;
    MPU->RASR = rasr;
}

STATIC INLINE VOID MpuRegionDisable(UINT32 region)
{
    MPU->RNR = region;
    MPU->RASR = 0;
}

/* Which box R3/R4 currently describe; NULL means they are retired (kernel map).
 *
 * This is deliberately NOT the running task's box. The dispatch asm
 * (BOX_PENDSV_RESTORE) skips BoxMpuSwitch on the box -> kernel edge, so the
 * mapping outlives that switch - harmless, since kernel tasks run privileged
 * and the box regions only constrain user access - but it means neither the
 * outgoing task's boxCB nor g_curBoxId can answer "what is in the registers
 * right now". Only this variable can, and comparing against it is what makes
 * a box -> kernel -> same-box round trip cost nothing. The asm keeps its own
 * new-vs-old boxCB compare as the fast path that skips the call entirely for
 * same-box task switches; this catches the round trip that compare cannot
 * see. Pointer equality does mean mapping equality: layout[0] comes from the
 * static config and does not move across a destroy/restart (no dynamic boxes
 * in this build - dynboxWord is 0 for both).
 *
 * CALL CONTRACT (pit 58): BoxMpuSwitch is ordinary AAPCS C - it may scratch
 * r0-r3, r12 and lr freely. The dispatch asm used to hold r1
 * (newTask->stackPointer) and r12 (PRIMASK) live across this BL unpushed; the
 * M6N-era codegen happened to use just r2/r3, and a cosmetic refactor that
 * moved the scratch to r1 crashed the first different-box restore of a
 * syscall-parked task (the guard's early return and the INUSER-path reload
 * had masked the clobber until a second box joined the schedule). The asm now
 * pushes r0/r1/r12/lr around the call (los_box_dispatch.h), so this function
 * is free to allocate registers however the compiler likes - the DO-NOT-TIDY
 * note that used to sit here recorded the wrong root cause and is gone. */
STATIC const LosBoxCB *g_mpuBoxCB = NULL;

VOID BoxMpuSwitch(const LosBoxCB *boxCB)
{
    if (boxCB == g_mpuBoxCB) {
        return;
    }

    if (boxCB == NULL) {
        /* Kernel task: the background map covers it, no box regions needed. */
        MpuRegionDisable(MPU_REGION_R3_BOX_CODE);
        MpuRegionDisable(MPU_REGION_R4_BOX_DATA);
    } else {
        MpuRegionSet(MPU_REGION_R3_BOX_CODE, boxCB->layout[0].boxTextStartAddr,
                     MPU_RASR_BOX_CODE);
        MpuRegionSet(MPU_REGION_R4_BOX_DATA, boxCB->layout[0].boxDataStartAddr,
                     MPU_RASR_BOX_DATA);
    }
    __DSB();
    __ISB();

    /* Recorded only once the hardware actually describes it: a call that dies
     * mid-programming must not leave the record claiming a mapping the MPU
     * never received. */
    g_mpuBoxCB = boxCB;
}

VOID MpuLayoutInit(VOID)
{
    HWI_PROC_FUNC *hwiForm = NULL;

    MPU->CTRL = 0;                                  /* configure disabled */

    /* R0: common code, executable from kernel and user boxes. */
    MpuRegionSet(MPU_REGION_R0_COMMON_TEXT, COMMON_FLASH_BASE, MPU_RASR_COMMON_TEXT);
    /* R1: common data, RW from both worlds. */
    MpuRegionSet(MPU_REGION_R1_COMMON_DATA, COMMON_RAM_BASE, MPU_RASR_COMMON_DATA);
    /* R2: user read-only data, written by kernel only. */
    MpuRegionSet(MPU_REGION_R2_USER_READONLY, USER_READONLY_RAM_BASE, MPU_RASR_USER_READONLY);
    /* R3/R4: box slots, retired until a box task is dispatched. This is also
     * the state g_mpuBoxCB's NULL initial value records. */
    MpuRegionDisable(MPU_REGION_R3_BOX_CODE);
    MpuRegionDisable(MPU_REGION_R4_BOX_DATA);

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
