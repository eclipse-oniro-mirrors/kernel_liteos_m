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

#ifndef _LOS_MEMORY_INTERNAL_H
#define _LOS_MEMORY_INTERNAL_H

#include "los_typedef.h"
#include "los_memory_pri.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct OsMemNodeHead {
#if (LOSCFG_BASE_MEM_NODE_INTEGRITY_CHECK == 1)
    UINT32 magic;
#endif
#if (LOSCFG_MEM_LEAKCHECK == 1)
    UINTPTR linkReg[LOSCFG_MEM_RECORD_LR_CNT];
#endif
    union {
        struct OsMemNodeHead *prev; /* The prev is used for current node points to the previous node */
        struct OsMemNodeHead *next; /* The next is used for sentinel node points to the expand node */
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

struct OsMemFreeNodeHead {
    struct OsMemNodeHead header;
    struct OsMemFreeNodeHead *prev;
    struct OsMemFreeNodeHead *next;
};

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _LOS_MEMORY_INTERNAL_H */
