/*
 * Copyright (c) 2021-2023 Huawei Device Co., Ltd. All rights reserved.
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

#include "los_config.h"
#include <sys/cdefs.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/reent.h>

/*
 * fs adapter interface, such as _read, see component/fs
 * time adapter interface, such as _gettimeofday, see posix/src/time.c
 * malloc adapter interface, such as _malloc_r, see posix/src/malloc.c
 */

int _isatty(int file)
{
    return (int)(file <= 2); // 2: stderr
}

int _kill(int i, int j)
{
    return 0;
}

int _getpid(void)
{
    return 0;
}

void _exit(int status)
{
    write(1, "exit\n", 5); // 1: stdout; 5: string length
    while (1) {
    }
}

/* sbrk for newlib malloc: use a DEDICATED region at the end of the board heap.
 *
 * IMPORTANT: LiteOS owns the system pool starting at __heap_start (via
 * LOS_MemInit on m_aucSysMem0 = LOSCFG_SYS_HEAP_ADDR). The board's
 * target_config.h reserves LOSCFG_NEWLIB_HEAP_SIZE bytes at the tail of
 * the heap for newlib by shrinking LOSCFG_SYS_HEAP_SIZE accordingly, so
 * the LiteOS pool is [__heap_start, +__heap_size - LOSCFG_NEWLIB_HEAP_SIZE)
 * and newlib malloc grows within [base, limit) defined below — the two never
 * overlap. This replaces the previous scheme that shared __heap_start and
 * corrupted pool metadata, crashing OsMemPoolHeadCheck when the shell
 * malloc'd during `help`. */
#ifndef LOSCFG_NEWLIB_HEAP_SIZE
#define LOSCFG_NEWLIB_HEAP_SIZE 0x10000  /* 64 KiB fallback */
#endif
static char *g_heapEnd = NULL;

void *_sbrk_r(struct _reent *reent, ptrdiff_t incr)
{
    char *limit = (char *)(UINTPTR)&__heap_start + (ptrdiff_t)(UINTPTR)&__heap_size;
    if (g_heapEnd == NULL) {
        g_heapEnd = limit - LOSCFG_NEWLIB_HEAP_SIZE;
    }
    char *prev = g_heapEnd;
    if (g_heapEnd + incr > limit) {
        reent->_errno = 12; /* ENOMEM */
        return (void *)-1;
    }
    g_heapEnd += incr;
    return prev;
}
