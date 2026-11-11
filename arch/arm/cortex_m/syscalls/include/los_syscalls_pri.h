#ifndef LOS_SYSCALLS_PRI_H
#define LOS_SYSCALLS_PRI_H

#include "string.h"
#include "los_task_pri.h"
#include "los_event.h"
#include "los_sem.h"
#include "los_perm.h"
/* TODO: other header files */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifdef LOSCFG_BASE_SYSCALL_PERM
extern BOOL OsSyscallIsPerm(UINTPTR handle);
#define OS_SYSCALL_IS_PERMITTED(handle)  OsSyscallIsPerm((UINTPTR)handle) 
#else
#define OS_SYSCALL_IS_PERMITTED(handle)  TRUE
#endif

#ifdef LOSCFG_COMPILER_ICCARM
#pragma section = ".syscall_addr_array"
#define SYSCALL_BEGIN    (UINTPTR)__section_begin(".syscall_addr_array")
#define SYSCALL_END    (UINTPTR)__section_end(".syscall_addr_array")
#endif

#define SWITCH_TO_SYSCALL_ADDR(name)  ({  \
    extern UINTPTR SYS_##name##_ADDR;  \
    &SYS_##name##_ADDR;
})

typedef struct tagFrame {
    UINT32 r0;
    UINT32 r1;
    UINT32 r2;
    UINT32 r3;
    UINT32 r4;
    UINT32 r5;
    UINT32 r6;
    UINT32 r7;
} SvcFrame;

struct fpuFrameContext {
    UINT32 s0;
    UINT32 s1;
    UINT32 s2;
    UINT32 s3;
    UINT32 s4;
    UINT32 s5;
    UINT32 s6;
    UINT32 s7;
    UINT32 s8;
    UINT32 s9;
    UINT32 s10;
    UINT32 s11;
    UINT32 s12;
    UINT32 s13;
    UINT32 s14;
    UINT32 s15;
    UINT32 fpscr;
    UINT32 res;
};

struct svcContext {
    UINT32 excReturn;
    UINT32 r0;
    UINT32 r1;
    UINT32 r2;
    UINT32 r3;
    UINT32 r12;
    UINT32 lr;
    UINT32 pc;
    UINT32 xpsr;
};

extern VOID OsExcSyscall(VOID);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif