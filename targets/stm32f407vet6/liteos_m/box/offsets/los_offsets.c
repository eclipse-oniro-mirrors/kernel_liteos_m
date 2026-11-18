/*
 * Linux-style asm-offsets source for the BOX feature.
 *
 * Dual duty:
 *  - compiled as a normal kernel source: the _Static_asserts below run on
 *    every build, so any TaskCB/TaskContext layout drift breaks the build;
 *  - compiled to assembly (gcc -S) by gen_offsets.py: the emitted
 *    @@BOX_OFFSET@@ lines become #defines in the generated los_offsets.h
 *    (consumed by the syscall entry / dispatch asm).
 *
 * The marker text starts with '@' which is a line comment in ARM assembly,
 * so the string is harmless when this file is assembled normally.
 */
#include "los_typedef.h"
#include "los_task_base.h"
#include "los_arch_context.h"
#include "los_arch_interrupt.h"  /* EXC_CONTEXT_S, for the FP frame geometry */
#include "los_box_pri.h"    /* LosBoxCB, for BOX_CB_LAYOUT0_MEMPOOL below */
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
    _Static_assert(TASK_CB_CUR_BOX_ID == __builtin_offsetof(LosTaskCB, curBoxId), "stale");
    _Static_assert(BOX_CB_LAYOUT0_MEMPOOL == __builtin_offsetof(LosBoxCB, layout[0].boxMemPoolAddr), "stale");
    _Static_assert(TASK_CTX_R4 == __builtin_offsetof(TaskContext, uwR4), "stale");
    _Static_assert(TASK_CTX_R0 == __builtin_offsetof(TaskContext, uwR0), "stale");
    _Static_assert(TASK_CTX_SIZE == sizeof(TaskContext), "stale");
    _Static_assert(EXC_CTX_FP_GAP == __builtin_offsetof(EXC_CONTEXT_S, uwR4), "stale");
    _Static_assert(EXC_CTX_FP_TAIL ==
                   (sizeof(EXC_CONTEXT_S) - __builtin_offsetof(EXC_CONTEXT_S, S0)), "stale");
    /* Status-bit value contract: los_task_base.h owns the C definition and
     * the generated asm-offsets header carries offsets and sizes only, never
     * a status bit. Asm consumers get the bit from los_box_dispatch.h, which
     * mirrors this value; the assert below is what catches the two definition
     * sites drifting apart. */
    _Static_assert(OS_TASK_STATUS_INUSER == 0x0200, "stale");
#endif

    /* TCB offsets consumed by the svc entry / dispatch asm. */
    BOX_OFFSET(TASK_CB_KERNEL_SP,        __builtin_offsetof(LosTaskCB, stackPointer));
    BOX_OFFSET(TASK_CB_STATUS,           __builtin_offsetof(LosTaskCB, taskStatus));
    BOX_OFFSET(TASK_CB_BOX_CB,           __builtin_offsetof(LosTaskCB, boxCB));
    BOX_OFFSET(TASK_CB_USER_SP,          __builtin_offsetof(LosTaskCB, userStackPointer));
    BOX_OFFSET(TASK_CB_SYSCALL_NEST_CNT, __builtin_offsetof(LosTaskCB, syscallNestCnt));
    BOX_OFFSET(TASK_CB_CUR_BOX_ID,       __builtin_offsetof(LosTaskCB, curBoxId));

    /* LosBoxCB offset: dispatch publishes the box pool address to the user
     * world (g_boxLocalPool) on box switches. */
    BOX_OFFSET(BOX_CB_LAYOUT0_MEMPOOL,   __builtin_offsetof(LosBoxCB, layout[0].boxMemPoolAddr));

    /* TaskContext offsets (uwR4 first, FPU block per the arch header). */
    BOX_OFFSET(TASK_CTX_R4,              __builtin_offsetof(TaskContext, uwR4));
    BOX_OFFSET(TASK_CTX_R0,              __builtin_offsetof(TaskContext, uwR0));
    BOX_OFFSET(TASK_CTX_SIZE,            sizeof(TaskContext));

    /* Exception-frame FP geometry: the S16-S31 (d8-d15) slot below uwR4, and
     * the S0-S15 + FPSCR + reserved tail above the hardware frame. Consumed by
     * los_dispatch_box.S (first dispatch) and cross-checked by los_box_task.c
     * (the C context crafter) - both used to carry literal 64/72/100, so a
     * struct change meant editing asm by hand. Deriving them here makes this
     * header the single source and turns drift into a build failure.
     *
     * These come out WIDE (64/72) in this TU even though it is compiled
     * without -mfpu: EXC_CONTEXT_S's FP condition has a LOSCFG_BOX arm, and
     * config.h is force-included (-imacros) while __FPU_PRESENT is 1 from the
     * CMSIS device header. TaskContext above has no such arm, which is why it
     * stays SHORT at 68 - and why the asm needs FP_GAP + TASK_CTX_R0 (=100)
     * rather than a single offsetof. */
    BOX_OFFSET(EXC_CTX_FP_GAP,           __builtin_offsetof(EXC_CONTEXT_S, uwR4));
    BOX_OFFSET(EXC_CTX_FP_TAIL,          (sizeof(EXC_CONTEXT_S) - __builtin_offsetof(EXC_CONTEXT_S, S0)));
#else
    BOX_OFFSET(BOX_DISABLED,             0);
#endif
}
