/* ----------------------------------------------------------------------------
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2013-2020. All rights reserved.
 * Description: Tick
 * Author: Huawei LiteOS Team
 * Create: 2013-01-01
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 * of conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific prior written
 * permission.
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
 * --------------------------------------------------------------------------- */

#include "los_typedef.h"
#include "los_hwi_pri.h"
#ifdef LOSCFG_SYSTEM_MONITOR
#include "los_monitor_pri.h"
#endif
#include "los_printf_pri.h"
#include "los_box_pri.h"
#include "los_task_pri.h"
#include "los_exc.h"

/* Adaptation record (refs kernel_s/usr/los_usr_init.c, wired 2026-11-17):
 *  - LITE_OS_SEC_RODATA_USRSPACE -> LITE_OS_SEC_USER_RO_DATA: refs' usr
 *    rodata section is COMMON_FLASH here (ROM), and every one of these
 *    globals is written at RUN time (the dispatch asm updates g_sysMode /
 *    g_curBoxId on every kernel<->user switch) - they must live in the R2
 *    RAM window the MPU maps kernel-W / user-RO (macros in los_box_pri.h).
 *  - LITE_OS_SEC_TEXT_USRROM -> LITE_OS_SEC_USRROM_TEXT: kernel TUs cannot
 *    spell the builddef_target name (kernel modules stay on the generic,
 *    definition-less los_builddef.h), so the .usrtcm.text placement rides
 *    the local macro in los_box_pri.h.
 *  - g_curBoxId keeps the volatile this tree added: the extern in
 *    los_box_pri.h and the readers are volatile-adapted; refs has it plain.
 *  - g_curPerBoxData has no consumer in this tree yet (refs' per-box-var
 *    module was not ported); unreferenced, --gc-sections drops it.
 *  - SYS_MODE_DEFAULT_KERNEL (refs: arch cpu.h) is defined in los_box_pri.h.
 *  - OS_IN_KER_STATE -> LOS_IN_KERNEL: refs' predicate calls
 *    LOS_CurTaskIDGet() (kernel flash) and reads g_intCount (kernel RAM) -
 *    BOTH unreachable from this function's USER-mode arm, the call would
 *    IACCVIOL before either print arm runs. g_sysMode (R2) carries the
 *    same kernel/user signal and is user-readable, so LOS_IN_KERNEL is the
 *    equivalent that works in both worlds. (Verified empirically: with the
 *    refs predicate the trip faulted at LOS_CurTaskIDGet, Type 9.) */
LITE_OS_SEC_USER_RO_DATA UINT32  g_sysMode = SYS_MODE_DEFAULT_KERNEL;
LITE_OS_SEC_USER_RO_DATA UINTPTR g_boxLocalPool[OS_BOX_MEM_LAYOUT_NUM] = {0};
LITE_OS_SEC_USER_RO_DATA volatile UINT32 g_curBoxId = OS_KERNEL_BOX;
LITE_OS_SEC_USER_RO_DATA UINTPTR g_curPerBoxData = 0;

#ifdef __GNUC__
/* stack protector: user-executable (COMMON_TEXT via .usrtcm.text) - a
 * canary trip in BOX code reaches this copy and prints + spins instead of
 * faulting into the kernel-flash copy. */
LITE_OS_SEC_USRROM_TEXT VOID __stack_chk_fail(VOID)
{
#ifdef LOSCFG_SYSTEM_MONITOR
    if (LOS_IN_KERNEL) {
        OsMonitorExcHook(OS_EXC_TYPE_STACK_FAULT);
    } else {
        (VOID)LOS_MonitorRaiseException(MONITOR_EXCEPTION_TYPE_STACK_FAULT);
    }
#endif
    if (LOS_IN_KERNEL) {
    /* __builtin_return_address is a builtin function, building in gcc */
        LOS_PANIC("stack-protector: function stack is corrupted in: %p\n",
              __builtin_return_address(0));
    } else {
    /* __builtin_return_address is a builtin function, building in gcc */
        PRINT_EXC("stack-protector: function stack is corrupted in: %p\n",
              __builtin_return_address(0));
    }
    for(;;) {

    }
}
#endif