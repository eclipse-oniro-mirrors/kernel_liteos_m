/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 * of conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific prior written
 * permission.
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

#ifndef IT_LOS_MEM_H
#define IT_LOS_MEM_H

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

#include "osTest.h"
#include "los_memory.h"
#include "los_config.h"
#include "iCunit.h"
#include "math.h"

#define OS_MEM_WATERLINE 1

#define LOS_DLNK_NODE_HEAD_SIZE 0
#define MIN_DLNK_POOL_SIZE      0

#ifdef OS_MEM_WATERLINE
#define TEST_MEM_MINIUSE (LOS_DLNK_HEAD_SIZE + LOS_DLNK_NODE_HEAD_SIZE + sizeof(LOS_MEM_POOL_INFO))
#endif

#define TEST_MEM_SIZE 0x2000

#define IS_ALIGNED_SIZE(value, alignSize) (0 == ((UINT32)(value) & (UINT32)((alignSize) - 1)))

#define TEST_POOL_SIZE (1 * 8 * 1024)

#define RANDOM(x) (rand() % (x))

#define LOS_INIT_MEM LOS_MemInit
#define LOS_ALLOC_MEM LOS_MemAlloc
#define LOS_REALLOC_MEM LOS_MemRealloc
#define LOS_FREE_MEM LOS_MemFree

/* Supposing a Second Level Index: SLI = 3. */
#define OS_MEM_SLI 3
/* Giving 1 free list for each small bucket: 4, 8, 12, up to 124. */
#define OS_MEM_SMALL_BUCKET_COUNT 31
#define OS_MEM_SMALL_BUCKET_MAX_SIZE 128
/* Giving OS_MEM_FREE_LIST_NUM free lists for each large bucket. */
#define OS_MEM_LARGE_BUCKET_COUNT 24
#define OS_MEM_FREE_LIST_NUM (1 << OS_MEM_SLI)
/* OS_MEM_SMALL_BUCKET_MAX_SIZE to the power of 2 is 7. */
#define OS_MEM_LARGE_START_BUCKET 7

/* The count of free list. */
#define OS_MEM_FREE_LIST_COUNT (OS_MEM_SMALL_BUCKET_COUNT + (OS_MEM_LARGE_BUCKET_COUNT << OS_MEM_SLI))
/* The bitmap is used to indicate whether the free list is empty, 1: not empty, 0: empty. */
#define OS_MEM_BITMAP_WORDS ((OS_MEM_FREE_LIST_COUNT >> 5) + 1)

struct TestMemNodeHead {
#if (LOSCFG_BASE_MEM_NODE_INTEGRITY_CHECK == 1)
    UINT32 magic;
#endif
#if (LOSCFG_MEM_LEAKCHECK == 1)
    UINTPTR linkReg[LOSCFG_MEM_RECORD_LR_CNT];
#endif
    union {
        struct TestMemNodeHead *prev; /* The prev is used for current node points to the previous node */
        struct TestMemNodeHead *next; /* The next is used for sentinel node points to the expand node */
    } ptr;
#if (LOSCFG_TASK_MEM_USED == 1)
    UINT32 taskId;
    UINT32 sizeAndFlag;
#elif (LOSCFG_MEM_FREE_BY_TASKID == 1)
    UINT32 taskId : 6;
    UINT32 sizeAndFlag : 26;
#else
    UINT32 sizeAndFlag;
#endif
};

struct TestMemUsedNodeHead {
    struct TestMemNodeHead header;
};

struct TestMemFreeNodeHead {
    struct TestMemNodeHead header;
    struct TestMemFreeNodeHead *prev;
    struct TestMemFreeNodeHead *next;
};

struct TestMemPoolInfo {
    VOID *pool;
    UINT32 totalSize;
    UINT32 attr;
#if (LOSCFG_MEM_WATERLINE == 1)
    UINT32 waterLine;   /* Maximum usage size in a memory pool */
    UINT32 curUsedSize; /* Current usage size in a memory pool */
#endif
};

struct TestMemPoolHead {
    struct TestMemPoolInfo info;
    UINT32 freeListBitmap[OS_MEM_BITMAP_WORDS];
    struct TestMemFreeNodeHead *freeList[OS_MEM_FREE_LIST_COUNT];
#if (LOSCFG_MEM_MUL_POOL == 1)
    VOID *nextPool;
#endif
};

#define LOS_MEM_NODE_HEAD_SIZE sizeof(struct TestMemUsedNodeHead)
#define MIN_MEM_POOL_SIZE (LOS_MEM_NODE_HEAD_SIZE + sizeof(struct TestMemPoolHead))
#define LOS_MEM_POOL_SIZE sizeof(struct TestMemPoolHead)

/*
 * bestfit (ws63) white-box offset copies, for fuzz cases ItLosMem328/329/330/331.
 * Layout mirrors kernel sources exactly (do not drift):
 *   - LosMemPoolInfo  : kernel/base/include/los_memory_pri.h:47-64 (bestfit branch)
 *   - LosMemCtlNode/LosMemDynNode : kernel/base/mem/bestfit/los_memory_internal.h:44-80
 * Memstat / LosSlabControlHeader / LosMultipleDlinkHead come from the real
 * private headers (kernel/base/include is on the test include path, precedent
 * It_los_mem_301.c includes los_slab_pri.h); only the bestfit-directory
 * structures need copies because mem/bestfit is not on the include path.
 * Guarded by LOSCFG_KERNEL_MEM_BESTFIT to avoid clashing with the TLSF copies
 * above on qemu builds.
 */
#ifdef LOSCFG_KERNEL_MEM_BESTFIT
#include "los_memstat_pri.h"
#include "los_slab_pri.h"
#include "los_multipledlinkhead_pri.h"

/* copy of bestfit LosMemPoolInfo (los_memory_pri.h:49-64) */
typedef struct {
    VOID *pool;
    UINT32 poolSize;
#ifdef LOSCFG_MEM_TASK_STAT
    Memstat stat;
#endif
#if (LOSCFG_MEM_MUL_POOL == 1)
    VOID *nextPool;
#endif
#ifdef LOSCFG_KERNEL_MEM_SLAB_EXTENTION
    struct LosSlabControlHeader slabCtrlHdr;
#endif
} TestMemPoolInfoBF;

/* copy of bestfit LosMemCtlNode (bestfit/los_memory_internal.h:44-72) */
typedef struct {
    union {
        LOS_DL_LIST freeNodeInfo;         /* free memory node */
        struct {
            UINTPTR magic;
            UINTPTR taskId : 16;
#ifdef LOSCFG_MEM_MUL_MODULE
            UINTPTR moduleId : 16;
#endif
        };
    };
    struct TestMemDynNodeBF *preNode; /* pointer to the previous memory node */
#ifdef LOSCFG_MEM_HEAD_BACKUP
    UINT32 gapSize;
    UINTPTR checksum; /* magic = xor checksum */
#endif
#ifdef LOSCFG_MEM_LEAKCHECK
    UINTPTR linkReg[LOSCFG_MEM_RECORD_LR_CNT];
#endif
#ifdef LOSCFG_AARCH64
    UINT32 reserve2; /* 64-bit alignment */
#endif
    /* size and flag of the current node (high two bits are flags) */
    UINT32 sizeAndFlag;
} TestMemCtlNodeBF;

/* copy of bestfit LosMemDynNode (bestfit/los_memory_internal.h:75-80) */
typedef struct TestMemDynNodeBFTag {
#ifdef LOSCFG_MEM_HEAD_BACKUP
    TestMemCtlNodeBF backupNode;
#endif
    TestMemCtlNodeBF selfNode;
} TestMemDynNodeBF;

/* bestfit node flag bits (bestfit/los_memory_internal.h:91-93) */
#define TEST_BF_NODE_HEAD_SIZE           sizeof(TestMemDynNodeBF)
#define TEST_BF_NODE_USED_FLAG           0x80000000U
#define TEST_BF_NODE_ALIGNED_FLAG        0x40000000U
#define TEST_BF_NODE_ALIGNED_AND_USED_FLAG (TEST_BF_NODE_USED_FLAG | TEST_BF_NODE_ALIGNED_FLAG)
/* OS_DLNK_HEAD_SIZE / LosMultipleDlinkHead come from los_multipledlinkhead_pri.h */
#endif /* LOSCFG_KERNEL_MEM_BESTFIT */

extern void *g_memPool;
extern void *g_testPool;

extern void MemStart(void);
extern void MemEnd(void);
extern void MemInit(void);
extern void MemFree(void);
extern UINT32 MemGetFreeSize(void *pool);
extern UINT32 CalPow(UINT32 exp);

VOID ItLosMem001(void);
VOID ItLosMem002(void);
VOID ItLosMem003(void);
VOID ItLosMem004(void);
VOID ItLosMem005(void);
VOID ItLosMem006(void);
VOID ItLosMem007(void);
VOID ItLosMem008(void);
VOID ItLosMem009(void);
VOID ItLosMem010(void);
VOID ItLosMem011(void);
VOID ItLosMem012(void);
VOID ItLosMem013(void);
VOID ItLosMem014(void);
VOID ItLosMem015(void);
VOID ItLosMem016(void);
VOID ItLosMem017(void);
VOID ItLosMem018(void);
VOID ItLosMem019(void);
VOID ItLosMem020(void);
VOID ItLosMem021(void);
VOID ItLosMem022(void);
VOID ItLosMem023(void);
VOID ItLosMem024(void);
VOID ItLosMem025(void);
VOID ItLosMem026(void);
VOID ItLosMem027(void);
VOID ItLosMem028(void);
VOID ItLosMem029(void);
VOID ItLosMem030(void);
VOID ItLosMem031(void);
VOID ItLosMem032(void);
VOID ItLosMem033(void);
VOID ItLosMem035(void);
VOID ItLosMem036(void);
VOID ItLosMem037(void);
VOID ItLosMem038(void);
VOID ItLosMem039(void);
VOID ItLosMem040(void);
VOID ItLosMem041(void);
VOID ItLosMem042(void);
VOID ItLosMem043(void);
VOID ItLosMem044(void);
VOID ItLosMem045(void);
VOID ItLosMem046(void);
VOID ItLosMem047(void);
VOID ItLosMem048(void);
VOID ItLosMem049(void);
VOID ItLosMem050(void);
VOID ItLosMem051(void);
VOID ItLosMem052(void);
VOID ItLosMem053(void);
VOID ItLosMem054(void);
VOID ItLosMem055(void);
VOID ItLosMem056(void);
VOID ItLosMem057(void);
VOID ItLosMem058(void);
VOID ItLosMem063(void);
VOID ItLosMem064(void);
VOID ItLosMem065(void);
VOID ItLosTick001(void);
VOID ItLosTick002(void);
VOID ItLosTick003(void);
VOID ItLosTick004(void);
VOID ItLosTick005(void);
VOID ItLosTick006(void);
VOID ItLosTick007(void);
VOID ItLosTick008(void);
VOID ItLosTick009(void);
VOID ItLosTick010(void);
VOID ItLosTick011(void);
VOID ItLosTick012(void);
VOID ItLosTick013(void);
VOID ItLosTick014(void);
VOID ItLosTick015(void);
VOID ItLosTick016(void);
VOID ItLosTick017(void);
VOID ItLosMem301(void);
VOID ItLosMem310(void);
VOID ItLosMem311(void);
VOID ItLosMem312(void);
VOID ItLosMem313(void);
VOID ItLosMem314(void);
VOID ItLosMem315(void);
VOID ItLosMem316(void);
VOID ItLosMem317(void);
VOID ItLosMem318(void);
VOID ItLosMem319(void);
VOID ItLosMem320(void);
VOID ItLosMem321(void);
VOID ItLosMem322(void);
VOID ItLosMem323(void);
VOID ItLosMem324(void);
VOID ItLosMem325(void);
VOID ItLosMem326(void);
VOID ItLosMem327(void);
VOID ItLosMem328(void);
VOID ItLosMem329(void);
VOID ItLosMem330(void);
VOID ItLosMem331(void);
VOID ItLosMem332(void);
#ifdef LOSCFG_MEM_MUL_POOL_ALLOC
#define TEST_POOL_MAX_NUM       4
#define TEST_EACH_POOL_SIZE     0x2000
extern UINT8 g_poolMem[TEST_POOL_MAX_NUM][TEST_EACH_POOL_SIZE];
extern UINT32 g_poolNum;
VOID TestGetAddrSize(UINTPTR *poolAddr, UINT32 *poolSize);
VOID TestReleaseAddrSize(VOID *pool);
VOID TEST_MulPoolUnRegister(VOID);
VOID ItLosMulPool001(void);
VOID ItLosMulPool002(void);
VOID ItLosMulPool003(void);
VOID ItLosMulPool004(void);
VOID ItLosMulPool005(void);
#endif

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

#endif /* IT_LOS_MEM_H */
