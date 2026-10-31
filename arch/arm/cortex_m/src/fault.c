#include "arch/exception.h"
#include "arch/mpu.h"
#include "los_memory_pri.h"
#include "los_task_pri.h"
#include "los_printf_pri.h"
#include "los_exc.h"
#include "nvic.h"
#ifdef LOSCFG_SYSTEM_MONITOR
#include "los_monitor_pri.h"
#endif
#ifdef LOSCFG_KERNEL_TRACE
#include "los_trace_pri.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define USGFAULT  ((UINT32)1 << 18)
#define BUSFAULT  ((UINT32)1 << 17)
#define MEMFAULT  ((UINT32)1 << 16)
#define DIV0FAULT  ((UINT32)1 << 4)
#define HARD_FAULT_IRQN    (-13)
#define MASK_16_BIT  16
#ifdef LOSCFG_EXC_ISOLATION
#define THREAD_MODE_ERR   (UINT32)0x8
#endif

#ifdef LOSCFG_BACKTRACE
#define OS_MAX_BACKTRACE      15U
#define THUM_OFFSET           2U
#define STACK_OFFSET          4U
#define MASK_OFFSET           2U
#define HIGH_8_OFFSET         8U
#define BL_CMD_OFFSET         4U
#define BLX_CMD_OFFSET        2U
#define PUSH_MASK_WITH_LR     0xb5U
#define PUSH_MASK             0xb4U
#define LOW_11_BITS_MASK      0x7FFU
#define HIGH_8_BITS_MASK      0xFF00U
#define SIGN_BIT_MASK         0x400000U
#define HIGH_OFFSET_NUMBER    12U
#define LOW_OFFSET_NUMBER     1U

#define BL_INS                0xF000F000U
#define BLX_INX               0x4700U

__attribute__((__used__)) STATIC UINTPTR g_textStart = (UINTPTR)&__text_start;
__attribute__((__used__)) STATIC UINTPTR g_textEnd = (UINTPTR)&__text_end;
__attribute__((__used__)) STATIC UINTPTR g_estack = (UINTPTR)&_estack;
STATIC VOID BackTraceWithSp(UINTPTR sp);
#endif

#define MPU_RASR_FIELD_ENABLE(val)   (0x1U & (val))
#define MPU_RASR_FIELD_AP(val)   ((0x7000000U & (val)) >> 24U)
#define MPU_RASR_FIELD_SIZE(val)   ((0x3EU & (val)) >> 1U)
#define MPU_RASR_FIELD_N_CALC(val)   ((UINT32)1U << ((val) + 1U))

#ifdef  LOSCFG_ARCH_INTERRUPT_TAKEOVER
#define NON_MASKABLE_IRQN       (-14)
#define HARD_FAULT_IRQN         (-13)
#define MEMORY_MANAGEMENT_IRQN  (-12)
#define BUSFAULT_IRQN         (-11)
#define USAGEFAULT_IRQN       (-10)
#define SVCALL_IRQN           (-5)
#endif

#ifdef LOSCFG_EXC_ISOLATION
LITE_OS_SEC_RODATA_USRSPACE UINT32 g_excSaveMsp = 0;
#endif

#ifdef LOSCFG_FS_EXC_OPERATION
LITE_OS_SEC_RODATA_USRSPACE UINT32 g_curNestCount = 0;
#else
LITE_OS_SEC_BSS UINT32 g_curNestCount = 0;
#endif
UINT8 g_excTbl[FAULT_STATUS_REG_BIT] = {
    0, 0, 0, 0, 0, 0, OS_EXC_UF_DIVBYZERO, OS_EXC_UF_UNALIGNED,
    0, 0, 0, 0, OS_EXC_UF_NOCP, OS_EXC_UF_INVPC, OS_EXC_UF_INVSTATE, OS_EXC_UF_UNDEFINSTR,
    0, 0, 0, OS_EXC_BF_STKERR, OS_EXC_BF_UNSTKERR, OS_EXC_BF_IMPRECISERR, OS_EXC_BF_PRECISERR, OS_EXC_BF_IBUSERR,
    0, 0, 0, OS_EXC_MF_MSTKERR, OS_EXC_MF_MUNSTKERR, 0, OS_EXC_MF_DACCVIOL, OS_EXC_MF_IACCVIOL
};
ExcInfoArray g_excArray[(CHAR)OS_EXC_TYPE_MAX - 1];
STATIC EXC_PROC_FUNC g_excHook = NULL;
ExcInfo g_excInfo;

BOOL ArchExcActive(VOID)
{
    UINT32 ipsr = ArchGetIpsr();
    return ipsr < 15 && ipsr > 0;
}

#ifdef LOSCFG_FS_EXC_OPERATION
LITE_OS_SEC_TEXT_USRSPACE BOOL ArchExcActiveUser(VOID)
{
    return (g_curNestCount != 0);
}
#endif

STATIC VOID OsExcSysInfo(VOID)
{
    const LosTaskCB *runTask = OsCurrTaskGet();

    if (runTask != NULL) {
        PRINT_EXC("TaskName = %s\n"
                  "TaskId = 0x%x\n",
                  runTask->taskName,
                  runTask->taskId);
        PRINT_EXC("Task stackSize = %u\n", (runTask->boxCB == NULL) ? runTask->stackSize : runTask->userStackSize);
    }
}

STATIC INLINE VOID OsMpuInfoDisPlay(VOID)
{
    UINT32 rnr;
    UINT32 rbar;
    UINT32 rasr;
    UINT32 size;
    UINT32 startAddr;

    for (rnr = 0; rnr < MPU_MAX_SUPPORT; rnr++) {
        WRITE_UINT32(rnr, OS_MPU_RNR);
        PRINT_EXC("\nRNR   = 0x%x\n", GET_UINT32(OS_MPU_RNR));

        rbar = GET_UINT32(OS_MPU_RBAR);
        rasr = GET_UINT32(OS_MPU_RASR);

        PRINT_EXC("RBAR   = 0x%x\n", GET_UINT32(OS_MPU_RBAR));
        PRINT_EXC("RASR   = 0x%x\n", GET_UINT32(OS_MPU_RASR));

        if (MPU_RASR_FIELD_ENABLE(rasr) != 0) {
            size = MPU_RASR_FIELD_SIZE(rasr);
            startAddr = rbar & (~(MPU_RASR_FIELD_N_CALC(size) - (UINT32)1));
            PRINT_EXC("\nAddr Range: 0x%x ~ 0x%x\n", startAddr, startAddr + MPU_RASR_FIELD_N_CALC(size));
            PRINT_EXC("AP : 0x%x\n", MPU_RASR_FIELD_AP(rasr));
        }
    }
}

LITE_OS_SEC_TEXT VOID OsExcInfoDisplay(const ExcInfo *exc, const ExcContext *excBufAddr)
{
    UINT32 i;
    const UINTPTR *reg = NULL;

    PRINT_EXC("Phase\t= fault in %s\n"
              "Type\t= 0x%x\n"
              "FaultAddr\t= 0x%x\n"
              "intNumOrTaskId\t= 0x%x\n",
        g_phaseName[exc->phase], exc->type, exc->faultAddr, exc->intNumOrTaskId);

    reg = &excBufAddr->R0;
    for (i = REG0_IDX; i < REG4_IDX; i++, reg++) {
        PRINT_EXC("R%u\t= 0x%x\n", i, *reg);
    }
    reg = &excBufAddr->R4;
    for (; i < REG12_IDX; i++, reg++) {
        PRINT_EXC("R%u\t= 0x%x\n", i, *reg);
    }
    PRINT_EXC("R12\t= 0x%x\n"
              "PriMask\t= 0x%x\n"
              "SP\t= 0x%x\n"
              "LR\t= 0x%x\n"
              "PC\t= 0x%x\n"
              "xPSR\t= 0x%x\n",
        excBufAddr->R12, excBufAddr->PriMask,
        excBufAddr->SP, excBufAddr->LR, excBufAddr->PC, excBufAddr->xPSR);

    /* todo: 打印其他寄存器*/
}

STATIC INLINE VOID OsExcBoxDestroy(VOID)
{
#ifdef LOSCFG_EXC_ISOLATION
    const struct LosTaskCBtag *runTask = ArchCurrTaskGet();
    const struct LosBoxCBtag *boxCB = runTask->boxCB;
    if ((g_excInfo.phase == OS_EXC_IN_TASK) && (boxCB != NULL) && 
       ((runTask->taskStatus & OS_TASK_STATUS_INUSER) != 0)) {
#ifdef LOSCFG_SYSTEM_MONITOR
        OsMonitorCpuException(boxCB->boxId);
#endif
        OsBoxDestroy(boxCB->boxId);
        return;   //  can't get here
    }
#endif
}

STATIC_INLINE VOID OsExcInfoRecord(UINT32 excType, UINT32 faultAddr, UINT32 pid,
                                   const ExcContext *excBufAddr)
{
    UINT16 tmpFlag = (UINT16)((excType >> MASK_16_BIT) & OS_NULL_SHORT);
    
    g_excInfo.nestCnt = (UINT16)g_curNestCount;
    g_excInfo.type = (UINT16)(excType & OS_NULL_SHORT);
    if ((tmpFlag & OS_EXC_FLAG_FAULTADDR_VALID) != 0) {
        g_excInfo.faultAddr = faultAddr;
    } else {
        g_excInfo.faultAddr = OS_EXC_IMPRECISE_ACCESS_ADDR;
    }

    if ((ArchCurrTaskGet() != NULL) && (((LosTaskCB *)ArchCurrTaskGet())->taskEntry != NULL)) {
        if ((tmpFlag & OS_EXC_FLAG_IN_HWI) !=0) {
            g_excInfo.phase = OS_EXC_IN_HWI;
            g_excInfo.intNumOrTaskId = pid;
        } else {
            g_excInfo.phase = OS_EXC_IN_TASK;
            g_excInfo.intNumOrTaskId = ((LosTaskCB *)ArchCurrTaskGet())->taskId;
#ifndef LOSCFG_EXC_SIMPLE_INFO
            OsExcSysInfo();
#endif
        }
    } else {
        g_excInfo.phase = OS_EXC_IN_INIT;
        g_excInfo.intNumOrTaskId = OS_NULL_INT;
    }

    if ((excType & OS_EXC_FLAG_NO_FLOAT) != 0) {
        g_excInfo.context = (const ExcContext *)((const CHAR *)excBufAddr - LOS_OFF_SET_OF(ExcContext, R4));
    } else {
        g_excInfo.context = (const ExcContext *)excBufAddr;
    }
}

LITE_OS_SEC_TEXT VOID OsExcHandleEntry(UINT32 excType, UINT32 faultAddr, UINT32 pid,
                                       const ExcContext *excBufAddr)
{
    const ExcContext *BufAddr = NULL;
    OsKernelDataEnable();
    g_curNestCount++;
    OsExcInfoRecord(excType, faultAddr, pid, excBufAddr);

    if (g_excHook != NULL) {
        g_excHook(excType, excBufAddr);
    }

    BufAddr = g_excInfo.context;
    OsExcInfoDisplay((const ExcInfo *)&g_excInfo, BufAddr);

#ifdef LOSCFG_BACKTRACE
    if (g_excInfo.phase == OS_EXC_IN_TASK) {
        BackTraceWithSp((UINTPTR)ArchGetPsp());
    } else {
        BackTraceWithSp((UINTPTR)ArchGetMsp());
    }
#endif

#ifdef LOSCFG_KERNEL_TRACE
    OsTraceRecordDump(FALSE);
#endif
    g_curNestCount--;

    OsExcBoxDestroy();
#ifdef LOSCFG_SYSTEM_MONITOR
    OsMonitorExcHook(MONITOR_EXCEPTION_CPU_EXCEPTION);
#endif
    for (;;) {

    }
}

UINT32 LOS_ExcRegHook(EXC_PROC_FUNC excHook)
{
    UINT32 intSave;

    intSave = ArchIntLock();
    g_excHook = excHook;
    ArchIntRestore(intSave);
    return 0;
}
    
#ifdef LOSCFG_BACKTRACE
/* todo backtarce 实现*/
STATIC INLINE UINTPTR LoopUntilEntry(UINTPTR addr)
{
    UINTPTR address = addr;
    while (address > (UINTPTR)g_textStart) {
        if (((*((UINT16 *)address) >> HIGH_8_OFFSET) == PUSH_MASK_WITH_LR) ||
            ((*((UINT16 *)address) >> HIGH_8_OFFSET) == PUSH_MASK)) {
            break;
        }
        address -= THUM_OFFSET;
    }

    return address;
}

STATIC INLINE UINTPTR CalculateBLTargetAddress(UINTPTR bl)
{
    UINTPTR target;
    UINT32 off0, off1, off;

    off0 = *(UINT16 *)bl & LOW_11_BITS_MASK;
    off1 = *(UINT16 *)(bl + MASK_OFFSET) & LOW_11_BITS_MASK;
    off = (off0 << HIGH_OFFSET_NUMBER) + (off1 << LOW_OFFSET_NUMBER);
    if ((off & SIGN_BIT_MASK) != 0) {
        target = bl + BL_CMD_OFFSET - ((~(off - 1)) & 0x7FFFFF); // 0x7FFFFF : offset mask
    } else {
        target = bl + BL_CMD_OFFSET + off;
    }

    return target;
}

STATIC INLINE UINTPTR CalculateTargetAddress(UINTPTR bl)
{
    UINTPTR target;
    STATIC UINTPTR tmpBL = 0;

    if ((((*(UINT16 *)(bl - BLX_CMD_OFFSET)) & HIGH_8_BITS_MASK) == BLX_INX)) {
        if (tmpBL != 0) {
            target = LoopUntilEntry(tmpBL);
            tmpBL = bl - BLX_CMD_OFFSET;
            return target;
        }
        tmpBL = bl - BLX_CMD_OFFSET;
        return LoopUntilEntry(tmpBL);
        /* Get 2 bytes twice to avoid unaligned access */
    } else if (((*(UINT16 *)(bl - BL_CMD_OFFSET + 2) & BL_INS) == BL_INS) &&
               ((*(UINT16 *)(bl - BL_CMD_OFFSET) & BL_INS) == BL_INS)) {
        tmpBL = bl - BL_CMD_OFFSET;
        return CalculateBLTargetAddress(tmpBL);
    }

    return 0;
}

#endif

VOID ArchBackTrace(VOID)
{
#ifdef LOSCFG_BACKTRACE
    UINTPTR sp = ArchGetSp();
    BackTraceWithSp(sp);
#endif   
}

VOID ArchBackTraceWithSp(const VOID *stackPointer)
{
#ifdef LOSCFG_BACKTRACE
    BackTraceWithSp((UINTPTR)stackPointer);
#else
    (VOID)stackPointer;
#endif
}

LITE_OS_SEC_TEXT_INIT VOID ArchExcInit(VOID)
{
#if defined(LOSCFG_ARCH_INTERRUPT_TAKEOVER)
    g_hwiVec[(INT32)SVCALL_IRQN + (INT32)OS_SYS_VECTOR_CNT] = OsExcSvcCall;
#endif
    *(volatile UINT32*)OS_NVIC_SHCSR |= (USGFAULT | BUSFAULT | MEMFAULT);
    *(volatile UINT32*)OS_NVIC_CCR |= DIV0FAULT;
}

#ifdef __cplusplus
}
#endif /* __cplusplus */