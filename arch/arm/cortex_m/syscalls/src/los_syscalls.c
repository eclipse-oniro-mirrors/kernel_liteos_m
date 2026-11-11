#include "los_syscalls_pri.h"
#include "los_builddef.h"
#ifdef LOSCFG_FS_VFS
#include "compiler.h"
#include "sys/stat.h"
#endif

#define ERR_BAD_SYSCALL   (-11)
#define UINT32_CUT_MASK    0xFFFFFFFFU 
#define OS_MV_32_BIT      32

typedef UINT64  (* SYSCALLS_FUNC)(UINT32, UINT32, UINT32, UINT32, UINT32, UINT32, UINT32, UINT32);

#ifdef LOSCFG_BASE_SYSCALL_PERM
LITE_OS_SEC_TEXT WEAK BOOL OsSyscallIsPerm(UINTPTR handle)
{
     if (handle < (UINTPTR)SWTICH_TO_SYSCALL_ADDR(LOS_TaskCreateStatic)) {
        return FALSE;
     }
     return TRUE;
}
#endif

STATIC INT32 OsSysInvalidSyscall(VOID)
{
    PRINT_ERR("invalid syscall\n");
    return ERR_BAD_SYSCALL;
}

LITE_OS_SEC_TEXT UINTPTR OsSyscallHandler(UINTPTR syscall)
{
    register SYSCALLS_FUNC sysfunc = (SYSCALLS_FUNC)(OsSysInvalidSyscall);
    if ((SYSCALL_BEGIN <= (UINTPTR)syscall) && ((UINTPTR)syscall < SYSCALL_END) && OS_SYSCALL_IS_PERMITTED(syscall)) {
        sysfunc = (SYSCALLS_FUNC)(* (UINTPTR *)(syscall & 0xFFFFFFFE));
    }
    return (UINTPTR)sysfunc;
}

BOOL IsParamAddrInUserspace(VOID)
{
    LosTaskCB *taskCB = (LosTaskCB *)ArchCurrTaskGet();
    if (taskCB->sysCallNestCnt > 1) {
        return FALSE;
    } 
    return TRUE;
}

UINT32 GetSyscallNestCntAddr(VOID)
{
    LosTaskCB *taskCB = (LosTaskCB *)ArchCurrTaskGet();
    return (UINT32)&taskCB->sysCallNestCnt;
}


#ifdef LOSCFG_BASE_IPC_KERNEL_BOXCALL
#define EXC_RETURN_FTYPE_MAST   0x10
#define XPSR_E_MASK             0x200

LITE_OS_SEC_TEXT VOID *OsSyscallFrameConstruction(VOID *destPoint, VOID *srcPoint)
{
    SvcFrame *svcFrame = NULL;
    struct svcContext *point = (struct svcContext *)srcPoint;
    UINT32 excReturn = point->excReturn;
    UINT32 pointLen = sizeof(struct svcContext);

    if (excReturn & EXC_RETURN_FTYPE_MAST == 0) {
        pointLen += sizeof(struct fpuFrameContext);
    }

    if (point->xpsr & XPSR_E_MASK == XPSR_E_MASK) {
        pointLen += sizeof(UINT32);
    }
    pointLen += sizeof(SvcFrame);
    (VOID)memcpy_s((VOID *)((UINTPTR)destPoint - pointLen), pointLen, srcPoint, pointLen);
    
    svcFrame = (SvcFrame *)((UINTPTR)destPoint - sizeof(SvcFrame));
    svcFrame->r0 = 0;
    svcFrame->r1 = 0;
    return (VOID *)((UINTPTR)destPoint - pointLen);
}
#endif
