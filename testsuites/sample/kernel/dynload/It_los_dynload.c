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
#include "los_ld_elflib.h"
#include "los_ld_initlib_pri.h"
#include "It_los_dynload.h"

/* Board hooks to ensure/release the test FS volume (weak default: mounted at boot). */
extern int TestFsVolumeEnsure(void);
extern int TestFsVolumeRelease(void);

VOID ItSuiteLosDynload(VOID)
{
    if (TestFsVolumeEnsure() != 0) {
        dprintf("ItSuiteLosDynload: TEST_ROOT volume unavailable, FS cases will fail\n");
    }

    /* Register the DSO search path for dep_a.so's bare DT_NEEDED name. */
    (VOID)LOS_PathAdd(DSO_PATH);

    /* Warm up the loader before the cases; no LOS_LdDestroy at suite end. */
    (VOID)LD_LoadLock();
    if (LD_IsLoaderInit() == FALSE) {
        (VOID)LD_Init();
    }
    LD_LoadUnlock();

    ItLosDynloadPrepareDsos();

    ItLosDynload001();
    ItLosDynload002();
    ItLosDynload003();
    ItLosDynload004();
    ItLosDynload005();
    ItLosDynload006();
    ItLosDynload007();
    ItLosDynload008();
    ItLosDynload009();
    ItLosDynload010();
    ItLosDynload011();
    ItLosDynload012();
    ItLosDynload013();
    ItLosDynload014();
    ItLosDynload015();
#if defined(KERNEL_BUG_14_DYNLOAD_SHN_COMMON) && KERNEL_BUG_14_DYNLOAD_SHN_COMMON == 1
    ItLosDynload016();
#endif
#if defined(KERNEL_BUG_15_DYNLOAD_PCREL_LO12) && KERNEL_BUG_15_DYNLOAD_PCREL_LO12 == 1
    ItLosDynload017();
#endif
#if defined(KERNEL_BUG_16_DYNLOAD_REL_REFCNT) && KERNEL_BUG_16_DYNLOAD_REL_REFCNT == 1
    ItLosDynload018();
#endif

    (VOID)TestFsVolumeRelease();
}
