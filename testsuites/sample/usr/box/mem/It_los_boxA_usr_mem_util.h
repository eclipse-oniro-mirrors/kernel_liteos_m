/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2026 Huawei Device Co., Ltd. All rights reserved.
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

/* ============================================================================
 * It_los_boxA_usr_mem_util.h - shared helpers for the usr-world BOX memory cases.
 *
 * One case per .c file, all in mem/. The helpers are declared here and DEFINED
 * ONCE in It_los_boxA_usr_mem_util.c - ordinary calls, not `static inline`:
 *
 *   - one definition instead of one copy per translation unit, so a fix lands
 *     in exactly one place;
 *   - the helper bodies can be breakpointed and stepped in a debugger, which an
 *     inlined body cannot (each call site would be a separate machine-code
 *     copy);
 *   - call sites stay readable: the trade-off is a call instruction per use,
 *     measured at roughly +1 KB of flash for this suite - cheap next to the
 *     ~29 KB still free in the boxA slot.
 *
 * The link-time cost is bounded because the boxA module is built with
 * -ffunction-sections -fdata-sections and linked --gc-sections: a helper that
 * no case calls is dropped, so unused ones cost nothing.
 *
 * These cases run in boxA USER mode and observe the BOX PRIVATE HEAP - the only
 * memory a box application owns dynamically. The heap is formatted by the box
 * entry task (usr_demo/tboxA/boxA_test_main.c: LOS_MemInit(OS_BOXA_POOL_ADDR,
 * BOXA_HEAP_SIZE)), so it is ready before this suite runs.
 *
 * Why user mode can observe it:
 *   - LOS_MemInfoGet / LOS_MemPoolSizeGet / LOS_MemAlloc / LOS_MemFree and the
 *     task APIs resolve to COMMON-section code (MPU R0, executable by a box);
 *   - g_boxLocalPool[] lives in R2 (kernel writes, box reads);
 *   - only the pool LOCK takes the mux syscalls, so these calls must stay in
 *     task context, never in an ISR.
 *
 * File names keep "boxA": boxes.ld routes objects by file name (*boxA*.o) and a
 * GN object is "<target>.<source>.o" regardless of subdirectory, which is why
 * mem/ needs no linker-script change.
 *
 * SILENT BY DESIGN: a case's verdict comes from the iCunit [Passed]/[Failed]
 * rows. Define BOX_MEM_VERBOSE to restore the per-step numbers (that costs
 * .rodata format strings plus the varargs calls, so it is off by default).
 * ========================================================================== */

#ifndef _IT_LOS_BOXA_MEM_UTIL_H
#define _IT_LOS_BOXA_MEM_UTIL_H

#include "osTest.h"       /* iCunit macros, los_memory.h, securec.h */
#include "syscalls.h"     /* usr-side prototypes + the rename #undef guard */
#include "boxes_conf.h"   /* OS_BOXA_POOL_ADDR / BOXA_HEAP_SIZE */

/* usr-world guard: under the kernel rename root LOS_MemAlloc is a macro that
 * binds the kernel-flash implementation, and an unprivileged fetch there is an
 * IACCVIOL. */
#ifdef LOS_MemAlloc
#error "usr test TU compiled under the kernel rename root: check BUILD.gn configs"
#endif

/* Verbose trace. The macro stays in the header (it is a compile-time switch),
 * but the helpers that print live in the .c file, so only that one object needs
 * rebuilding when you flip it. */
/* Off by default: a case's verdict comes from the iCunit [Passed]/[Failed]
 * row, and the boxA slot cannot afford a format string per trace point.
 * Define BOX_MEM_VERBOSE=1 (here or -DBOX_MEM_VERBOSE=1) to get the per-step
 * numbers back when diagnosing. NOTE the comparison, not #ifdef: the macro is
 * always defined, so #ifdef would leave the trace permanently on. */
#ifndef BOX_MEM_VERBOSE
#define BOX_MEM_VERBOSE 0
#endif

#if (BOX_MEM_VERBOSE == 1)
#define BOX_MEM_TRACE(...)  dprintf(__VA_ARGS__)
#else
#define BOX_MEM_TRACE(...)  do { } while (0)
#endif

/* ----------------------------------------------------------------------------
 * Address-space accounting as OsMemInfoGet reports it
 *
 *   OsMemInfoGet walks  n = FIRST_NODE(pool); n < END_NODE(pool); n = NEXT(n)
 *   and SEEDS totalUsedSize with sizeof(LosMemPoolInfo) + OS_DLNK_HEAD_SIZE.
 *
 * Two consequences, both measured on this board:
 *
 *  1) The END (sentinel) node sits AT the loop bound, so it is never walked: it
 *     is in neither total, yet its bytes are inside poolSize. Hence
 *         used + free + sentinel == poolSize      (not used + free == poolSize)
 *     Measured: 224 + 10924 + 16 = 11164 = BOXA_HEAP_SIZE. The 224 is exactly
 *     the seed (pool head 16 + dlink head 208), which also proves no other used
 *     node exists.
 *
 *  2) A freshly formatted pool has usedNodeNum == 0, not 1. The seed covers the
 *     headers and the sentinel is outside the walk.
 *
 * The sentinel size is mirrored here because los_memory_internal.h is
 * bestfit-private and not on the usr include path; the identity above catches
 * drift if the real node header changes.
 * -------------------------------------------------------------------------- */
#define BOX_MEM_NODE_HEAD_SIZE  16U
#define BOX_MEM_SENTINEL_SIZE   BOX_MEM_NODE_HEAD_SIZE

/* User stack the task cases use: 0x400 is the smallest the kernel accepts from
 * a box (LOSCFG_BASE_CORE_TSK_MIN_STACK_SIZE) and is what the box's own swtmr
 * task takes - the realistic unit. */
#define BOX_MEM_TSK_STACK_SIZE  0x400U

/* Probe priority: the box entry runs at 0, so 5 never preempts a case body -
 * probes run only while the body blocks on LOS_TaskDelay, keeping every
 * measurement point deterministic. */
#define BOX_MEM_TSK_PRIO        5U

/* Poll budget. This board ticks at ~1s (pre-existing SysTick quirk), so 10
 * rounds x 2 ticks is a ~20s worst case for a stuck child. */
#define BOX_MEM_TSK_SPIN        10U
#define BOX_MEM_TSK_SPIN_TICKS  2U

/* Loop counts stay small: every LOS_TaskDelay costs a second here. */
#define BOX_MEM_TSK_ROUNDS      6U

/* Current box heap pool (R2; republished by the PendSV restore on each switch). */
extern UINTPTR g_boxLocalPool[];

/* User-stack recycler (kernel/box/los_usr_api.c). No public prototype exists:
 * it is a usr-world wrapper (loops OsTaskGetRecycleStack, svc 39, then frees
 * through the COMMON allocator), not a syscall entry. */
extern VOID LOS_TaskStackRecycle(VOID);

/* ============================================================================
 * Snapshot
 * ========================================================================== */
typedef struct {
    UINTPTR pool;       /* pool the snapshot was taken from                    */
    UINT32  poolSize;   /* LOS_MemPoolSizeGet(pool)                            */
    UINT32  used;       /* totalUsedSize: header seed + walked used nodes      */
    UINT32  freeSize;   /* totalFreeSize: sum of walked free node sizes        */
    UINT32  maxFree;    /* maxFreeNodeSize: largest single free node           */
    UINT32  usedNodes;  /* usedNodeNum (0 right after LOS_MemInit)             */
    UINT32  freeNodes;  /* freeNodeNum (1 right after LOS_MemInit)             */
    UINT32  payload;    /* freeSize - node header = bytes a caller may request */
    BOOL    valid;      /* FALSE when the pool query was refused               */
} BoxMemSnap;

/* Gather a snapshot of the calling box's heap. FALSE when no box is current
 * (g_boxLocalPool[0] == 0) or the pool was refused - the caller must then fail
 * rather than compare garbage. `tag` is only used by the verbose trace. */
extern BOOL BoxMemSnapGet(BoxMemSnap *snap, const CHAR *tag);

/* Field-for-field equality (payload is derived, valid is bookkeeping). */
extern BOOL BoxMemSame(const BoxMemSnap *a, const BoxMemSnap *b);

/* Record one broken relation; returns 1 so callers can accumulate:
 *     fails += BoxMemFail("MEM-X-001", "what", actual, expect); */
extern UINT32 BoxMemFail(const CHAR *caseName, const CHAR *what,
                         UINT32 actual, UINT32 expect);

/* Cost of one plain allocation as the pool records it:
 *     ALIGN(size + node header, OS_MEM_ALIGN_SIZE) */
extern UINT32 BoxMemAllocCost(UINT32 size);

/* Cost of one aligned allocation, mirroring LOS_MemAllocAlign:
 *     useSize = size + boundary - sizeof(gapSize)
 *     cost    = ALIGN(useSize + node header, OS_MEM_ALIGN_SIZE) */
extern UINT32 BoxMemAlignAllocCost(UINT32 size, UINT32 boundary);

/* ============================================================================
 * Probe-task plumbing
 *
 * Every task case follows one rhythm:
 *   1. snapshot;  2. create a probe whose user stack comes from this heap;
 *   3. let it run and exit (it RETURNS - the box exit trampoline deletes it);
 *   4. drain the queued stack with LOS_TaskStackRecycle;  5. snapshot, compare.
 *
 * Steps 3-4 are asynchronous, hence BoxMemTskSettle: "back to baseline" is only
 * meaningful once the TCB returned AND the stack was drained.
 * ========================================================================== */

/* Create one probe. Its user stack comes from the box heap via the usr wrapper,
 * which is the cost under measurement. The kernel error code is returned
 * verbatim so callers can tell TSK_TCB_UNAVAILABLE (TCB pool) from
 * TSK_NO_MEMORY (heap or kernel-stack pool). */
extern UINT32 BoxMemTskCreate(TSK_ENTRY_FUNC entry, const CHAR *name,
                              UINT32 stackSize, UINT32 *taskId);

/* Poll a case-local flag until it reaches `expect`, ticking the scheduler so a
 * lower-priority probe actually runs. */
extern BOOL BoxMemTskWaitFlag(const volatile UINT32 *flag, UINT32 expect);

/* Drain queued stacks and wait for the pool to match `baseline`.
 * `out` may be NULL; otherwise it receives the last snapshot, even on failure. */
extern BOOL BoxMemTskSettle(const BoxMemSnap *baseline, BoxMemSnap *out);

/* Write a pattern into `len` bytes of `buf` and read it back - proves the user
 * stack is real, writable memory owned by this box. */
extern BOOL BoxMemPatternCheck(UINT8 *buf, UINT32 len, UINT32 seed);

#endif /* _IT_LOS_BOXA_MEM_UTIL_H */
