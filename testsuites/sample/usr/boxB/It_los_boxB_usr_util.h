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
 * It_los_boxB_usr_util.h - shared helpers for the boxB user-mode cases.
 *
 * boxB is the SECOND box: same isolation model as boxA (MPU regions, SVC
 * gateway, per-box pools, SID handles) but a very different resource budget:
 *
 *   FLASH  64K at 0x08030000      (same as boxA)
 *   RAM    16K at 0x2001C000      (same as boxA)
 *     .bss  ~4.1K  (g_boxbStack 4096 + a few counters)
 *     heap  ~12.2K <-- BIGGER than boxA's ~7K
 *
 *   resources:
 *     TASK  2   entry + swtmr task = FULL, no new tasks
 *     MUX   1   the heap lock takes it -> 0 usable
 *     SEM   0   EVENT 0   COND 0   QUEUE 0
 *     SWT   2
 *     SID   1   the heap lock takes it -> 0 usable
 *
 * That budget defines what this suite can be: PURE HEAP. No IPC, no task
 * creation. In exchange it can do something boxA cannot - tear its own heap
 * down and rebuild it - because boxB has no other consumer to starve.
 *
 * Everything here mirrors the boxA helper header (same measurement approach:
 * snapshot + delta), but trimmed to the heap-only surface.
 * ========================================================================== */

#ifndef _IT_LOS_BOXB_USR_UTIL_H
#define _IT_LOS_BOXB_USR_UTIL_H

#include "osTest.h"       /* iCunit macros, los_memory.h, securec.h */
#include "syscalls.h"     /* usr-side prototypes + the rename #undef guard */
#include "boxes_conf.h"   /* OS_BOXB_POOL_ADDR / BOXB_HEAP_SIZE */

/* usr-world guard: under the kernel rename root LOS_MemAlloc is a macro that
 * binds the kernel-flash implementation, and an unprivileged fetch there is an
 * IACCVIOL. */
#ifdef LOS_MemAlloc
#error "boxB usr TU compiled under the kernel rename root: check BUILD.gn configs"
#endif

/* Verbose trace, off by default (the boxB flash slot is as tight as boxA's). */
#ifndef BOX_MEM_VERBOSE
#define BOX_MEM_VERBOSE 0
#endif

#if (BOX_MEM_VERBOSE == 1)
#define BOXB_TRACE(...)  dprintf(__VA_ARGS__)
#else
#define BOXB_TRACE(...)  do { } while (0)
#endif

/* Failure reporting is NOT a trace and is never compiled out: a failing case
 * that prints only "Errline: N" loses the broken invariant. Learned the hard
 * way in the boxA suite, where BoxMemFail was silent for several rounds. */
#define BOXB_FAIL_LOG(...)  dprintf(__VA_ARGS__)

/* ----------------------------------------------------------------------------
 * Accounting model (identical to boxA - same bestfit allocator):
 *
 *   OsMemInfoGet walks  n = FIRST_NODE; n < END_NODE; n = NEXT(n)
 *   and seeds totalUsedSize with sizeof(LosMemPoolInfo) + OS_MULTI_DLNK_HEAD_SIZE.
 *
 * Two consequences:
 *  1) the sentinel node sits AT the loop bound and is never walked, so
 *         used + free + sentinel == poolSize      (NOT used + free == poolSize)
 *  2) a freshly formatted pool reports usedNodeNum == 0, not 1 - the seed
 *     already covers the headers.
 *
 * The sentinel size is mirrored here because los_memory_internal.h is
 * bestfit-private and off the usr include path; the identity in the cases
 * catches drift.
 * -------------------------------------------------------------------------- */
#define BOXB_NODE_HEAD_SIZE  16U
#define BOXB_SENTINEL_SIZE   BOXB_NODE_HEAD_SIZE

/* Node sizeAndFlag bits, mirrored from bestfit/los_memory_internal.h. A request
 * whose size carries either bit is refused by LOS_MemAlloc. */
#define BOXB_NODE_USED_FLAG      0x80000000U
#define BOXB_NODE_ALIGNED_FLAG   0x40000000U

/* Current box heap pool (R2; republished by the PendSV restore on each switch).
 * g_boxLocalPool is shared by ALL boxes - whichever box is running sees its own
 * pool here, which is exactly what lets this suite be box-agnostic. */
extern UINTPTR g_boxLocalPool[];

/* ============================================================================
 * Snapshot
 * ========================================================================== */
typedef struct {
    UINTPTR pool;
    UINT32  poolSize;
    UINT32  used;
    UINT32  freeSize;
    UINT32  maxFree;
    UINT32  usedNodes;
    UINT32  freeNodes;
    UINT32  payload;
    BOOL    valid;
} BoxBSnap;

/* Gather a snapshot of the calling box's heap. FALSE when no box is current or
 * the pool query was refused - the caller must then fail rather than compare
 * garbage. */
extern BOOL BoxBSnapGet(BoxBSnap *snap);

/* Field-for-field equality (payload is derived, valid is bookkeeping). */
extern BOOL BoxBSame(const BoxBSnap *a, const BoxBSnap *b);

/* Record one broken relation; returns 1 so callers can accumulate:
 *     fails += BoxBFail("BOX-B-001", "what", actual, expect); */
extern UINT32 BoxBFail(const CHAR *caseName, const CHAR *what,
                       UINT32 actual, UINT32 expect);

/* Cost of one plain allocation as the pool records it:
 *     ALIGN(size + node header, OS_MEM_ALIGN_SIZE) */
extern UINT32 BoxBAllocCost(UINT32 size);

/* Cost of one aligned allocation, mirroring LOS_MemAllocAlign. */
extern UINT32 BoxBAlignAllocCost(UINT32 size, UINT32 boundary);

/* Print a snapshot (verbose only). */
extern VOID BoxBSnapDump(const BoxBSnap *s, const CHAR *tag);

#endif /* _IT_LOS_BOXB_USR_UTIL_H */
