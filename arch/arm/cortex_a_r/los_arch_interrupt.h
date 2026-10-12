/*
 * Copyright (c) 2024 morpheus
 */

#ifndef _LOS_ARCH_INTERRUPT_H
#define _LOS_ARCH_INTERRUPT_H

#include "los_common_interrupt.h"
#include "soc.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

#ifndef OS_HWI_PRIO_HIGHEST
#define OS_HWI_PRIO_HIGHEST                   0
#endif

#ifndef OS_HWI_PRIO_LOWEST
#define OS_HWI_PRIO_LOWEST                    7
#endif

#define OS_SYS_VECTOR_CNT                     0
#define OS_VECTOR_CNT                         (OS_SYS_VECTOR_CNT + OS_HWI_MAX_NUM)

typedef INT32 IRQn_Type;

#define PSR_MODE_SVC_ARM   (0x13U | 0xC0U) /* SVC mode | I | F */

/*
 * GIC register definitions now live in the driver-level headers:
 *   drivers/interrupt/include/gic_common.h  — struct GicDistributor + GICD ptr
 *   drivers/interrupt/include/gic_v2.h     — struct GicCpuInterface + GICC ptr
 * The board's soc.h provides GIC_BASE_ADDR, GICD_OFFSET, GICC_OFFSET.
 * Include gic_v2.h from the GICv2 driver source to access these registers.
 */

extern VOID HalIrqInitPercpu(VOID);

/* ARM Generic Timer (physical) PPI IRQ number */
#define OS_GIC_TIMER_IRQ                       29

extern VOID HalInterrupt(VOID);
extern VOID HalHwiInit(VOID);

#define OS_EXC_FLAG_NO_FLOAT                   0x10000000

typedef struct TagExcContext {
    UINT32 uwR0;
    UINT32 uwR1;
    UINT32 uwR2;
    UINT32 uwR3;
    UINT32 uwR4;
    UINT32 uwR5;
    UINT32 uwR6;
    UINT32 uwR7;
    UINT32 uwR8;
    UINT32 uwR9;
    UINT32 uwR10;
    UINT32 uwR11;
    UINT32 uwR12;
    UINT32 uwSP;
    UINT32 uwLR;
    UINT32 uwPC;
    UINT32 uwxPSR;
} EXC_CONTEXT_S;

typedef VOID (*EXC_PROC_FUNC)(UINT32, EXC_CONTEXT_S *);

extern EXC_PROC_FUNC g_excRegHook;
extern UINT32 ArchSetExcHook(EXC_PROC_FUNC excHook);

VOID HalExcHandleEntry(UINT32 excType, UINT32 faultAddr, UINT32 pid, EXC_CONTEXT_S *excBufAddr);

#define OS_EXC_CAUSE_UNDEF                     0
#define OS_EXC_CAUSE_PABT                      1
#define OS_EXC_CAUSE_DABT                      2
#define OS_EXC_CAUSE_UNUSED                    3
#define OS_EXC_CAUSE_NMI                       16
#define OS_EXC_CAUSE_HARDFAULT                17
#define OS_EXC_CAUSE_TASK_EXIT                18
#define OS_EXC_CAUSE_FATAL_ERR                19

#define OS_EXC_FLAG_FAULTADDR_VALID            0x01
#define OS_EXC_FLAG_IN_HWI                     0x02
#define OS_EXC_IMPRECISE_ACCESS_ADDR           0xABABABAB
#define OS_EXC_EVENT                           0x00000001

#define OS_EXC_MAX_BUF_LEN                     25
#define OS_EXC_MAX_NEST_DEPTH                   1

typedef struct TagExcInfo {
    UINT16 phase;
    UINT16 type;
    UINT32 faultAddr;
    UINT32 thrdPid;
    UINT16 nestCnt;
    UINT16 reserved;
    EXC_CONTEXT_S *context;
} ExcInfo;

extern ExcInfo g_excInfo;
extern UINT32 g_curNestCount;

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

#endif /* _LOS_ARCH_INTERRUPT_H */
