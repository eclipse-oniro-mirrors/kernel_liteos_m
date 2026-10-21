/*
 * Copyright (c) 2026-2026 Huawei Device Co., Ltd. All rights reserved.
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

/* Test DSO images generated at build time (see BUILD.gn) and unpacked to DSO_PATH at runtime. */

#include "It_los_dynload.h"
#include "los_elf_symbol_pri.h"
#include "los_tables.h"
#include "errno.h"
#include "fcntl.h"
#include "limits.h"
#include "securec.h"
#include "string.h"
#include "sys/stat.h"
#include "unistd.h"

#ifndef PATH_MAX
#define PATH_MAX 256
#endif

/* Kernel-side value exported to loaded modules through the OS symbol table. */
INT32 g_dynload_os_export = 4242;

/* Kernel-side write targets for relocation-store verification: on W^X boards
 * (hi3322, PMP entry19) the module window is either RW (loading) or RX (running),
 * never both, so a module writing its own data after ArchDynloadFinish faults.
 * Store-family relocations (LO12_S stores, JAL/BRANCH targets) therefore write
 * these kernel-side globals (DTCM, RW) resolved through the OS symbol table.
 * OS symbol table entries must stay sorted by name (binary search). */
INT32 g_dynload_krel_branch_hit = 0;
INT32 g_dynload_krel_jal_hit = 0;
INT32 g_dynload_krel_store = 0;

STATIC LDSymbol g_dynloadKrelBranchHitSym LOS_HAL_TABLE_ENTRY(dynload_gsymbol) = {
    .name = "g_dynload_krel_branch_hit",
    .addr = (const INT8 *)&g_dynload_krel_branch_hit,
};

STATIC LDSymbol g_dynloadKrelJalHitSym LOS_HAL_TABLE_ENTRY(dynload_gsymbol) = {
    .name = "g_dynload_krel_jal_hit",
    .addr = (const INT8 *)&g_dynload_krel_jal_hit,
};

STATIC LDSymbol g_dynloadKrelStoreSym LOS_HAL_TABLE_ENTRY(dynload_gsymbol) = {
    .name = "g_dynload_krel_store",
    .addr = (const INT8 *)&g_dynload_krel_store,
};

STATIC LDSymbol g_dynloadOsSymEntry LOS_HAL_TABLE_ENTRY(dynload_gsymbol) = {
    .name = "g_dynload_os_export",
    .addr = (const INT8 *)&g_dynload_os_export,
};

/* Image boundaries of the lib/ files, provided by the objcopy objects. */
extern const UINT8 _binary_basic_so_start[];
extern const UINT8 _binary_basic_so_end[];
extern const UINT8 _binary_dep_a_so_start[];
extern const UINT8 _binary_dep_a_so_end[];
extern const UINT8 _binary_dep_b_so_start[];
extern const UINT8 _binary_dep_b_so_end[];
extern const UINT8 _binary_exec_so_start[];
extern const UINT8 _binary_exec_so_end[];
extern const UINT8 _binary_ossym_so_start[];
extern const UINT8 _binary_ossym_so_end[];
extern const UINT8 _binary_shell_sh_start[];
extern const UINT8 _binary_shell_sh_end[];
extern const UINT8 _binary_objbasic_o_start[];
extern const UINT8 _binary_objbasic_o_end[];
extern const UINT8 _binary_objmovwt_o_start[];
extern const UINT8 _binary_objmovwt_o_end[];
extern const UINT8 _binary_objossym_o_start[];
extern const UINT8 _binary_objossym_o_end[];
extern const UINT8 _binary_objundef_o_start[];
extern const UINT8 _binary_objundef_o_end[];
extern const UINT8 _binary_reloc_o_start[];
extern const UINT8 _binary_reloc_o_end[];
extern const UINT8 _binary_relocc_o_start[];
extern const UINT8 _binary_relocc_o_end[];

typedef struct {
    const CHAR *fileName;
    const UINT8 *start;
    const UINT8 *end;
} DsoImage;

STATIC const DsoImage g_dsoImages[] = {
    { "basic.so", _binary_basic_so_start, _binary_basic_so_end },
    { "dep_a.so", _binary_dep_a_so_start, _binary_dep_a_so_end },
    { "dep_b.so", _binary_dep_b_so_start, _binary_dep_b_so_end },
    { "exec.so", _binary_exec_so_start, _binary_exec_so_end },
    { "ossym.so", _binary_ossym_so_start, _binary_ossym_so_end },
    { "shell.sh", _binary_shell_sh_start, _binary_shell_sh_end },
    { "objbasic.o", _binary_objbasic_o_start, _binary_objbasic_o_end },
    { "objmovwt.o", _binary_objmovwt_o_start, _binary_objmovwt_o_end },
    { "objossym.o", _binary_objossym_o_start, _binary_objossym_o_end },
    { "objundef.o", _binary_objundef_o_start, _binary_objundef_o_end },
    { "reloc.o", _binary_reloc_o_start, _binary_reloc_o_end },
    { "relocc.o", _binary_relocc_o_start, _binary_relocc_o_end },
};

/* Write the embedded DSO images into DSO_PATH so the cases can load them. */
VOID ItLosDynloadPrepareDsos(VOID)
{
    UINT32 i, dirLen;
    INT32 fd;
    CHAR dir[PATH_MAX];
    CHAR path[PATH_MAX];

    if (strcpy_s(dir, sizeof(dir), DSO_PATH) != EOK) {
        return;
    }
    dirLen = strlen(dir);
    if ((dirLen > 0) && (dir[dirLen - 1] == '/')) {
        dir[--dirLen] = '\0';
    }
    if (mkdir(dir, 0777) != 0) {
        dprintf("ItLosDynloadPrepareDsos: mkdir %s failed, errno: %d\n", dir, errno);
        return;
    }

    for (i = 0; i < sizeof(g_dsoImages) / sizeof(g_dsoImages[0]); i++) {
        const DsoImage *img = &g_dsoImages[i];
        if (sprintf_s(path, sizeof(path), "%s%s", DSO_PATH, img->fileName) < 0) {
            continue;
        }
        fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0777);
        if (fd < 0) {
            dprintf("ItLosDynloadPrepareDsos: open %s failed, errno: %d\n", path, errno);
            continue;
        }
        if (write(fd, img->start, (UINT32)(img->end - img->start)) != (img->end - img->start)) {
            dprintf("ItLosDynloadPrepareDsos: write %s failed, errno: %d\n", path, errno);
        }
        (VOID)close(fd);
    }
}
