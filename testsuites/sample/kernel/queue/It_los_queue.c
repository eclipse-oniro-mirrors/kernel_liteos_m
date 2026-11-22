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

#include "It_los_queue.h"

/* Queue cases split along the handle-identity model. Under BOX every handle
 * is an encoded sId (M6K layout), under BOX=off it is a plain pool index,
 * and 20 cases assert semantics that only exist for the pool index:
 *
 *  1. NOT_CREATE on "slot exists but not created": they poke a raw small id
 *     (hardcoded 2, the pre-create residue, id+1) or a stale id after delete
 *     and expect LOS_ERRNO_QUEUE_NOT_CREATE. The sId world never reaches the
 *     queueState check on such a handle - OsSIdValidate/OsSIdObjDel reject
 *     it first with QUEUE_INVALID / NOT_FOUND (OsQueueDelete resolves before
 *     it checks state). Affected: 004 005 019 028 041 053 058 096 097 104
 *     117(head: 007 030 040, isr: 002, static: 005).
 *  2. Static queues: LOS_QueueCreateStatic cannot bind a static-pool CB
 *     under BOX (los_queue.c OsQueueCreateOut: "Static-pool queues cannot
 *     exist under BOX anyway" - the create exit cannot bind a CB outside
 *     both pools) and fails with LOS_ERRNO_SEC_INVALID_SIDMODULE. Affected:
 *     static 001 003 004.
 *  3. ItLosQueue301 asserts delete->create REUSES the same handle (an ABA
 *     exposure case); the sId carries a fresh generation salt precisely to
 *     prevent that, so the new handle differs by design.
 *
 * The guards below keep every one of these cases registered on BOX=off
 * builds, where those semantics hold - they are vanilla-semantics
 * regression material, not dead code. */
#ifdef LOSCFG_BOX
#define QUEUE_VANILLA_ONLY(call)
#else
#define QUEUE_VANILLA_ONLY(call) call
#endif

/* ItLosQueue088 builds one queue holding 1000 x 12B messages: a 12KB
 * CONTIGUOUS heap block by design. The coverage build's kernel heap is a
 * fixed 64K slot and the gcov counters leave ~27K (19K after the main
 * test task's enlarged stack), and the preceding cases' alloc/free churn
 * fragments the remainder below the requirement - the create fails with
 * QUEUE_NO_MEMORY. The case keeps running everywhere the heap affords it;
 * only the small-heap coverage build skips it. */
#if defined(LOSCFG_TEST_KERNEL_COVERAGE) && defined(LOSCFG_PLATFORM_STM32F407VET6)
#define QUEUE_COV_SKIP(call)
#else
#define QUEUE_COV_SKIP(call) call
#endif


VOID ItSuiteLosQueue(VOID)
{
    ItLosQueue001();
    ItLosQueue002();
    ItLosQueue003();
    QUEUE_VANILLA_ONLY(ItLosQueue004());
    QUEUE_VANILLA_ONLY(ItLosQueue005());
    ItLosQueue006();
    ItLosQueue007();
    ItLosQueue008();
    ItLosQueue009();
    ItLosQueue010();
    ItLosQueue011();
    ItLosQueue012();
    ItLosQueue013();
    ItLosQueue014();
    ItLosQueue015();
    ItLosQueue016();
    ItLosQueue017();
    ItLosQueue018();
    QUEUE_VANILLA_ONLY(ItLosQueue019());
    ItLosQueue020();
    ItLosQueue021();
    ItLosQueue022();
    ItLosQueue025();
    ItLosQueue026();
    ItLosQueue027();
    QUEUE_VANILLA_ONLY(ItLosQueue028());
    ItLosQueue029();
    ItLosQueue030();
    ItLosQueue031();
    ItLosQueue032();
    ItLosQueue033();
    ItLosQueue034();
    ItLosQueue035();
    ItLosQueue036();
    ItLosQueue037();
    ItLosQueue038();
    ItLosQueue039();
    ItLosQueue040();
    QUEUE_VANILLA_ONLY(ItLosQueue041());
    ItLosQueue042();
    ItLosQueue043();
    ItLosQueue051();
    ItLosQueue052();
    QUEUE_VANILLA_ONLY(ItLosQueue053());
    ItLosQueue054();
    ItLosQueue055();
    ItLosQueue056();
    ItLosQueue057();
    QUEUE_VANILLA_ONLY(ItLosQueue058());
    ItLosQueue059();
    ItLosQueue060();
    ItLosQueue061();
    ItLosQueue062();
    ItLosQueue063();
    ItLosQueue064();
    ItLosQueue065();
    ItLosQueue066();
    ItLosQueue067();
    ItLosQueue068();
    ItLosQueue069();
    ItLosQueue070();
    ItLosQueue071();
    ItLosQueue072();
    ItLosQueue074();
    ItLosQueue078();
    ItLosQueue079();
    ItLosQueue080();
    ItLosQueue081();
    ItLosQueue082();
    ItLosQueue083();
    ItLosQueue084();
    ItLosQueue085();
    ItLosQueue086();
    ItLosQueue087();
    QUEUE_COV_SKIP(ItLosQueue088());
    ItLosQueue090();
    QUEUE_VANILLA_ONLY(ItLosQueue096());
    QUEUE_VANILLA_ONLY(ItLosQueue097());
    QUEUE_VANILLA_ONLY(ItLosQueue104());
    ItLosQueue106();
    ItLosQueue107();
    ItLosQueue114();
    ItLosQueue115();
    ItLosQueue116();
    QUEUE_VANILLA_ONLY(ItLosQueue117()); /* OsQueueMailAlloc/OsQueueMailFree 邮件内存机制 */
    ItLosQueueHead001();
    ItLosQueueHead002();
    ItLosQueueHead003();
    ItLosQueueHead004();
    ItLosQueueHead005();
    ItLosQueueHead006();
    QUEUE_VANILLA_ONLY(ItLosQueueHead007());
    ItLosQueueHead008();
    ItLosQueueHead009();
    ItLosQueueHead010();
    ItLosQueueHead011();
    ItLosQueueHead012();
    ItLosQueueHead013();
    ItLosQueueHead014();
    ItLosQueueHead015();
    ItLosQueueHead016();
    ItLosQueueHead019();
    ItLosQueueHead020();
    ItLosQueueHead021();
    ItLosQueueHead022();
    ItLosQueueHead023();
    ItLosQueueHead024();
    ItLosQueueHead025();
    ItLosQueueHead026();
    ItLosQueueHead027();
    ItLosQueueHead028();
    ItLosQueueHead029();
    QUEUE_VANILLA_ONLY(ItLosQueueHead030());
    ItLosQueueHead031();
    ItLosQueueHead032();
    ItLosQueueHead038();
    ItLosQueueHead039();
    QUEUE_VANILLA_ONLY(ItLosQueueHead040());
    ItLosQueueHead041();
    ItLosQueueHead042();
#if (LOS_KERNEL_HWI_TEST == 1)
    ItLosQueueIsr001();
    QUEUE_VANILLA_ONLY(ItLosQueueIsr002());
    ItLosQueueIsr003();
    ItLosQueueIsr004();
    ItLosQueueIsr005();
    ItLosQueueIsr006();
    ItLosQueueIsr007();
    ItLosQueueIsr008();
    ItLosQueueIsr009();
#if (LOS_KERNEL_MULTI_HWI_TEST == 1)
    ItLosQueueIsr010();
#endif
    ItLosQueueIsr011();
    ItLosQueueIsr012();
    ItLosQueueIsr013();
    ItLosQueueIsr014();
    ItLosQueueIsr015();
    ItLosQueueIsr016();
#endif
#if (LOS_KERNEL_TEST_FULL == 1)
#if (LOS_KERNEL_HWI_TEST == 1)
#ifndef LOSCFG_BOX
    /* box not support OS_INT_ACTIVE create sid */
    ItLosQueue044();
    ItLosQueue045();

#if (LOS_KERNEL_MULTI_HWI_TEST == 1)
    ItLosQueue046();
#endif
    ItLosQueue048();
#endif
    ItLosQueue047();
    ItLosQueue050();
    ItLosQueue108();
#endif
    ItLosQueue073();
    ItLosQueue109();
    LlTLosQueue001();
    LltLosQueue003();
    ItLosQueue091();
#ifndef LOSCFG_BOX
    /* box not support OS_INT_ACTIVE create sid */
    ItLosQueue092();
    ItLosQueue093();
    ItLosQueue095();
#endif
    ItLosQueue094();
    ItLosQueue110();
#endif
#if (LOSCFG_QUEUE_STATIC_ALLOCATION == 1)
    QUEUE_VANILLA_ONLY(ItLosQueueStatic001());
    ItLosQueueStatic002();
    QUEUE_VANILLA_ONLY(ItLosQueueStatic003());
    QUEUE_VANILLA_ONLY(ItLosQueueStatic004());
    QUEUE_VANILLA_ONLY(ItLosQueueStatic005());
    ItLosQueueStatic006();
#endif
    QUEUE_VANILLA_ONLY(ItLosQueue301());
    ItLosQueueHead301();
    ItLosQueueHead302();
    ItLosQueueIsr301();
    ItLosQueueIsr302();
}
