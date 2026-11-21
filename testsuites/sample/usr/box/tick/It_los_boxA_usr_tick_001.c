/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted under the following conditions:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* ItLosBoxUsrTick001 - usr-world tick readouts through the syscall face:
 * LOS_CurrNanosec (UINT64) and LOS_GetCpuCycle (hi/lo out-args).
 *
 * Runs in boxA USER mode. CurrNanosec is the only 64-bit-return syscall:
 * the kernel epilogue writes r0 AND r1 back into the veneer's args block
 * and the veneer's pop {r0-r3} restores both - a broken 64-bit path shows
 * up here as a huge or wrap-around delta, which the bounds below catch.
 *
 *  phase 1 - two CurrNanosec reads across a 3-tick delay: strictly
 *     increasing and the delta bounded well below a second (nanoseconds)
 *  phase 2 - two GetCpuCycle reads across the same delay: the hi:lo pair
 *     strictly increases as one 64-bit value; hi alone may stay flat */

#include "osTest.h"
#include "syscalls.h"

#ifdef LOS_TaskCreate
#error "usr test TU compiled under the kernel rename root: check BUILD.gn configs"
#endif

static UINT32 TestCase(VOID)
{
    UINT64 ns1;
    UINT64 ns2;
    UINT32 hi1 = 0;
    UINT32 lo1 = 0;
    UINT32 hi2 = 0;
    UINT32 lo2 = 0;
    UINT32 fails = 0;

    /* ---- phase 1: CurrNanosec monotonic across a delay ---- */
    /* This board's tick measures ~1s (documented quirk, see uts_phase.h),
     * so a 3-tick delay spans ~3.1s of nanoseconds - bound generously at
     * 8s: the point is monotonicity plus a sane magnitude, not timing. */
    ns1 = LOS_CurrNanosec();
    LOS_TaskDelay(3);
    ns2 = LOS_CurrNanosec();
    if ((ns2 <= ns1) || ((ns2 - ns1) > (UINT64)8000000000)) {
        fails++;
        dprintf("usr tick ns1 0x%x%x ns2 0x%x%x\n",
                (UINT32)(ns1 >> 32), (UINT32)ns1, (UINT32)(ns2 >> 32), (UINT32)ns2);
    }

    /* ---- phase 2: GetCpuCycle hi:lo monotonic across a delay ---- */
    LOS_GetCpuCycle(&hi1, &lo1);
    LOS_TaskDelay(3);
    LOS_GetCpuCycle(&hi2, &lo2);
    if (!((hi2 > hi1) || ((hi2 == hi1) && (lo2 > lo1)))) {
        fails++;
        dprintf("usr tick cyc1 0x%x:0x%x cyc2 0x%x:0x%x\n", hi1, lo1, hi2, lo2);
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxUsrTick001(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrTick001", TestCase, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
