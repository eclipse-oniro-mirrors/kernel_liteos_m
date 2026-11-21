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
 * 3. Neither the name of the copyright holder nor its contributors may be used
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

/* ItLosBox005 - LOS_SwtmrUsrInit from a KERNEL-context task: the bring-up
 * resolves the owning box from the CALLER's context, and a kernel task has
 * none - the call must be refused with LOS_ERRNO_SWTMR_INVALID_BOXCB
 * before touching any box pool. The complementary usr-side phases (the
 * singleton LOS_ERRNO_SWTMR_INIT refusal from inside boxA) live in the
 * usr suite (ItLosBoxUsrSwtmr001).
 * Kernel mode, no box context. */

#include "osTest.h"
#include "los_swtmr_usr_pri.h"   /* LOS_SwtmrUsrInit + errno contract */

static UINT32 TestCase(VOID)
{
    UINT32 ret;

    ret = LOS_SwtmrUsrInit(NULL, 1024);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_SWTMR_INVALID_BOXCB, ret);
    return LOS_OK;
}

VOID ItLosBox005(VOID)
{
    TEST_ADD_CASE("ItLosBox005", TestCase, TEST_LOS, TEST_TASK, TEST_LEVEL1, TEST_FUNCTION);
}
