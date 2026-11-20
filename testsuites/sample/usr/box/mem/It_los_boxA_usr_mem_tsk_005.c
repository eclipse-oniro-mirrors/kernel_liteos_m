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
 * MEM-TSK-005  Static-stack task never touches the box heap   (group B)
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS CASE MEANS
 * ---------------------------------------------------------------------------
 * A box task's user stack normally comes from the box heap (MEM-TSK-001). There
 * is a second form: LOS_TaskCreateStatic, where the CALLER supplies the user
 * stack out of its own static memory. A box uses this when the stack size is
 * known at compile time and it wants to keep the heap for genuinely dynamic
 * data.
 *
 * The invariant under test is that a static-stack task is COMPLETELY invisible
 * to the heap, at every stage of its life:
 *
 *   create  -> zero heap change   (the stack is the caller's array)
 *   exit    -> zero heap change   (nothing is queued for recycling)
 *   drain   -> zero heap change   (the recycler has nothing for it)
 *
 * The third point is the one that matters most and is easiest to get wrong. A
 * dynamic-stack task's address is pushed onto boxResRecycleStackList and later
 * handed to LOS_MemFree. If a STATIC stack were ever queued by mistake, the
 * recycler would call LOS_MemFree on an address inside .boxA.bss - either the
 * free would be rejected (benign) or, worse, it would corrupt a pool whose
 * header is nowhere near that address. So this case is a guard against a
 * mis-classification in the delete path (OsTaskResRelease checks
 * `staticStack == 0` before queueing).
 *
 * ---------------------------------------------------------------------------
 * HOW TO USE
 * ---------------------------------------------------------------------------
 * The caller-provided stack lives in .boxA.bss - i.e. inside the box's own RAM
 * slot, which is required: the kernel validates that the user stack falls inside
 * the box data window (OsUsrTaskCreateParamCheck). A stack from anywhere else
 * would be rejected with LOS_ERRNO_TSK_ID_INVALID, which itself is worth seeing.
 *
 * The case PASSES when the heap snapshot is IDENTICAL before create, after
 * create, after exit, and after an explicit drain. It asserts nothing about
 * where the stack actually is - that is the kernel's business, and the fact
 * that the create succeeds at all already proves the address passed validation.
 * ========================================================================== */

#include "It_los_boxA_usr_mem_util.h"

/* Caller-provided user stack. LOS_TASK_STACK_ALIGN keeps the 8-byte alignment
 * the kernel demands; `used` stops the compiler from dropping it. The section
 * attribute puts it in the box's own RAM slot - without it the address would
 * land outside the box data window and the create would be refused. */
LOS_TASK_STACK_ALIGN static UINT8 g_t5StaticStack[BOX_MEM_TSK_STACK_SIZE]
    __attribute__((section(".boxA.bss"), used));

static volatile UINT32 g_t5Ran;
static volatile UINT32 g_t5Alive;

static VOID MemTsk005Entry(VOID)
{
    /* Touch the provided stack so it is provably usable, then leave. */
    (VOID)BoxMemPatternCheck(g_t5StaticStack, 64U, 0x5A5A0005U);
    g_t5Ran = 1;
    g_t5Alive--;
}

static UINT32 MemTsk005Case(VOID)
{
    static const CHAR *caseName = "MEM-TSK-005";
    BoxMemSnap s0;
    BoxMemSnap sCreate;
    BoxMemSnap sDel;
    BoxMemSnap sDrain;
    UINT32 taskId = 0;
    UINT32 ret;
    UINT32 fails = 0;

    /* ---- 1) baseline ---- */
    if (!BoxMemSnapGet(&s0, "MEM-TSK-005 init")) {
        ICUNIT_ASSERT_EQUAL(1, 0, 0);
        return LOS_OK;
    }
    dprintf("[MEM] MEM-TSK-005: static user stack at %p (%u bytes, .boxA.bss) "
            "- the heap must not move at all\n",
            (VOID *)g_t5StaticStack, (UINT32)sizeof(g_t5StaticStack));

    /* ---- 2) create with the caller-provided stack ---- */
    g_t5Ran = 0;
    g_t5Alive = 1;
    {
        TSK_INIT_PARAM_S param;

        (VOID)memset_s(&param, sizeof(param), 0, sizeof(param));
        param.pfnTaskEntry = (TSK_ENTRY_FUNC)MemTsk005Entry;
        param.uwStackSize  = BOX_MEM_TSK_STACK_SIZE;
        param.pcName       = "memTsk005";
        param.usTaskPrio   = BOX_MEM_TSK_PRIO;
        param.uwResved     = 0;

        /* LOS_TaskCreateStatic both creates and resumes, exactly like
         * LOS_TaskCreate - the only difference is where the stack comes from. */
        ret = LOS_TaskCreateStatic(&taskId, &param, (VOID *)g_t5StaticStack);
    }
    if (ret != LOS_OK) {
        g_t5Alive = 0;
        /* The likely causes, in order: the stack left the box data window
         * (ID_INVALID), or the TCB / kernel-stack pool is exhausted. */
        dprintf("[MEM] MEM-TSK-005 FAIL: LOS_TaskCreateStatic err 0x%x "
                "(stack outside box data window? pools exhausted?)\n", ret);
        ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
        return LOS_OK;
    }
    dprintf("[MEM] MEM-TSK-005: static-stack task created taskId=0x%x\n", taskId);

    /* ---- 3) create must not have touched the heap ---- */
    if (!BoxMemSnapGet(&sCreate, "MEM-TSK-005 after-create")) {
        fails++;
    } else {
        if (!BoxMemSame(&s0, &sCreate)) {
            fails += BoxMemFail(caseName, "heap changed on static-stack create",
                                sCreate.freeSize, s0.freeSize);
        } else {
            dprintf("[MEM] MEM-TSK-005: create left the heap untouched (as designed)\n");
        }
    }

    /* ---- 4) let it exit ---- */
    if (!BoxMemTskWaitFlag(&g_t5Ran, 1)) {
        dprintf("[MEM] MEM-TSK-005 FAIL: static-stack child never ran\n");
        fails++;
    }
    (VOID)BoxMemTskWaitFlag(&g_t5Alive, 0);
    LOS_TaskDelay(BOX_MEM_TSK_SPIN_TICKS);

    /* ---- 5) exit must not have touched the heap either ---- */
    if (!BoxMemSnapGet(&sDel, "MEM-TSK-005 after-del")) {
        fails++;
    } else {
        if (!BoxMemSame(&s0, &sDel)) {
            fails += BoxMemFail(caseName, "heap changed on static-stack exit",
                                sDel.freeSize, s0.freeSize);
        }
    }

    /* ---- 6) and a drain must find nothing for it ----
     * This is the sharp end: if the delete path wrongly queued the static
     * address, the drain would try to free it and the heap WOULD move. */
    LOS_TaskStackRecycle();
    if (!BoxMemSnapGet(&sDrain, "MEM-TSK-005 after-drain")) {
        fails++;
    } else {
        if (!BoxMemSame(&s0, &sDrain)) {
            fails += BoxMemFail(caseName,
                                "drain changed the heap - static stack was mis-queued",
                                sDrain.freeSize, s0.freeSize);
        } else {
            dprintf("[MEM] MEM-TSK-005: drain found nothing to free "
                    "(static stack correctly kept out of the recycle queue)\n");
        }
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxUsrMemTsk005(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrMemTsk005", MemTsk005Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
