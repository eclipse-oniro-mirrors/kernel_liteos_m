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
 * It_los_mutex_032.c - kernel-domain LOS_MuxHeld and SID shape.
 *
 * Design doc 06, 3.1 / 4.2.1: the protection enhancement adds LOS_MuxHeld for
 * both domains and requires a created mux's SID to carry the creating task's
 * domain. The old kernel mux cases exercise create/delete/pend/post but never
 * LOS_MuxHeld; the box mux cases exercise the usr side. This case closes the
 * kernel-side gap:
 *
 *   - a kernel task's mux carries OS_GET_SID_BOXID == LOS_KERNEL_SID_HEAD
 *     and OS_GET_SID_TYPE == LOS_SID_TYPE_MUX;
 *   - LOS_MuxHeld is 0 before the first pend, 1 while recursively held
 *     (regardless of recursion depth), and 0 again after the last post;
 *   - the stale handle left by delete reports "not held" instead of
 *     dereferencing a recycled CB.
 *
 * LOS_MuxHeld only exists when the BOX/SID shape is compiled (los_mux_pri.h),
 * hence the guard: a non-BOX kernel build keeps its old mux suite unchanged.
 * ========================================================================== */

#include "It_los_mux.h"

#ifdef LOSCFG_BOX

static UINT32 Mux032Case(VOID)
{
    UINT32 mux = 0;
    UINT32 ret;
    UINT32 fails = 0;

    ret = LOS_MuxCreate(&mux);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* Domain and module fields are what the box syscall seam uses to decide
     * which pool owns the CB. A kernel task must produce the kernel head. */
    if (OS_GET_SID_BOXID(mux) != (UINT16)LOS_KERNEL_SID_HEAD) {
        fails++;
    }
    if (OS_GET_SID_TYPE(mux) != (UINT32)LOS_SID_TYPE_MUX) {
        fails++;
    }

    if (LOS_MuxHeld(mux) != 0U) {
        fails++;                         /* fresh mux has no owner */
    }

    ret = LOS_MuxPend(mux, LOS_WAIT_FOREVER);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    if (LOS_MuxHeld(mux) != 1U) {
        fails++;
    }

    /* Recursion depth must not change the held answer: it is an ownership
     * predicate, not a count. */
    ret = LOS_MuxPend(mux, LOS_WAIT_FOREVER);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    if (LOS_MuxHeld(mux) != 1U) {
        fails++;
    }

    ret = LOS_MuxDelete(mux);
    if (ret != LOS_ERRNO_MUX_PENDED) {
        fails++;                         /* held mux must not be recycled */
    }

    ret = LOS_MuxPost(mux);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    if (LOS_MuxHeld(mux) != 1U) {
        fails++;                         /* one recursion level remains */
    }

    ret = LOS_MuxPost(mux);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    if (LOS_MuxHeld(mux) != 0U) {
        fails++;
    }

    ret = LOS_MuxDelete(mux);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* A stale sId must read as "not held", not as some other task's mux. */
    if (LOS_MuxHeld(mux) != 0U) {
        fails++;
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosMux032(void)
{
    TEST_ADD_CASE("ItLosMux032", Mux032Case, TEST_LOS, TEST_MUX, TEST_LEVEL0, TEST_FUNCTION);
}

#endif /* LOSCFG_BOX */
