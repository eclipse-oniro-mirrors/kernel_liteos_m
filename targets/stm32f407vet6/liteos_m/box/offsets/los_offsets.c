/*
 * Linux-style asm-offsets source for the BOX feature.
 *
 * Dual duty:
 *  - compiled as a normal kernel source: the _Static_asserts below run on
 *    every build, so any TaskCB/TaskContext layout drift breaks the build;
 *  - compiled to assembly (gcc -S) by gen_offsets.py: the emitted
 *    @@BOX_OFFSET@@ lines become #defines in the generated los_offsets.h
 *    (consumed by the ported syscall entry / dispatch asm, M4).
 *
 * The marker text starts with '@' which is a line comment in ARM assembly,
 * so the string is harmless when this file is assembled normally.
 */
#include "los_typedef.h"
#include "los_task_base.h"
#include "los_arch_context.h"
#ifndef GEN_OFFSETS   /* generation run skips the staleness check */
#include "../kernel/los_offsets.h"   /* generated; checked below */
#endif

#define BOX_OFFSET(sym, val) \
    asm volatile("\n@@BOX_OFFSET@@ " #sym " %c0" : : "i" ((val)))

/* Layout contract: dispatch asm assumes the kernel stack pointer at offset 0. */
_Static_assert(__builtin_offsetof(LosTaskCB, stackPointer) == 0,
               "dispatch asm assumes stackPointer at offset 0");

/* Never called: exists so gcc -S emits the offsets (Linux asm-offsets style).
 * External linkage keeps it alive at -O2; --gc-sections drops it from links. */
VOID OsBoxOffsetsEmit(VOID)
{
#ifdef LOSCFG_BOX
    /* Staleness guard: if the generated header lags the struct,
     * the build fails here. Regenerate: box/offsets/gen_offsets.py. */
#ifndef GEN_OFFSETS
    _Static_assert(TASK_CB_KERNEL_SP == __builtin_offsetof(LosTaskCB, stackPointer), "stale");
    _Static_assert(TASK_CB_STATUS == __builtin_offsetof(LosTaskCB, taskStatus), "stale");
    _Static_assert(TASK_CB_BOX_CB == __builtin_offsetof(LosTaskCB, boxCB), "stale");
    _Static_assert(TASK_CB_USER_SP == __builtin_offsetof(LosTaskCB, userStackPointer), "stale");
    _Static_assert(TASK_CB_SYSCALL_NEST_CNT == __builtin_offsetof(LosTaskCB, syscallNestCnt), "stale");
    _Static_assert(TASK_CTX_R4 == __builtin_offsetof(TaskContext, uwR4), "stale");
    _Static_assert(TASK_CTX_R0 == __builtin_offsetof(TaskContext, uwR0), "stale");
    _Static_assert(TASK_CTX_SIZE == sizeof(TaskContext), "stale");
    _Static_assert(OS_TASK_STATUS_INUSER == 0x0200, "stale");
#endif

    /* TCB offsets consumed by the ported svc entry / dispatch asm (old names). */
    BOX_OFFSET(TASK_CB_KERNEL_SP,        __builtin_offsetof(LosTaskCB, stackPointer));
    BOX_OFFSET(TASK_CB_STATUS,           __builtin_offsetof(LosTaskCB, taskStatus));
    BOX_OFFSET(TASK_CB_BOX_CB,           __builtin_offsetof(LosTaskCB, boxCB));
    BOX_OFFSET(TASK_CB_USER_SP,          __builtin_offsetof(LosTaskCB, userStackPointer));
    BOX_OFFSET(TASK_CB_SYSCALL_NEST_CNT, __builtin_offsetof(LosTaskCB, syscallNestCnt));

    /* TaskContext offsets (OHOS layout: uwR4 first, FPU block per arch header). */
    BOX_OFFSET(TASK_CTX_R4,              __builtin_offsetof(TaskContext, uwR4));
    BOX_OFFSET(TASK_CTX_R0,              __builtin_offsetof(TaskContext, uwR0));
    BOX_OFFSET(TASK_CTX_SIZE,            sizeof(TaskContext));

    /* User-mode status bit: 0x0200, same value the SECURE path uses for its
     * user-task flag, so the two mechanisms can share the bit. */
    BOX_OFFSET(OS_TASK_STATUS_INUSER,    0x0200);
#else
    BOX_OFFSET(BOX_DISABLED,             0);
#endif
}
