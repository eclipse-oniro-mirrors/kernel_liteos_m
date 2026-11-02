/*
 * Copyright (c) 2022-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to
 *    endorse or promote products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* fuzz suite aggregator: every module suite (los/cmsis/posix) in one entry,
 * called once from the fuzz-only branch of osTest.c TestTaskEntry */
#include "osTest.h"

extern VOID ItSuiteFuzzLosTask(VOID);
extern VOID ItSuiteFuzzLosSem(VOID);
extern VOID ItSuiteFuzzLosQueue(VOID);
extern VOID ItSuiteFuzzLosMux(VOID);
extern VOID ItSuiteFuzzLosEvent(VOID);
extern VOID ItSuiteFuzzLosSwtmr(VOID);
extern VOID ItSuiteFuzzLosMem(VOID);
extern VOID ItSuiteFuzzLosMisc(VOID);
extern VOID ItSuiteFuzzCmsisThread(VOID);
extern VOID ItSuiteFuzzCmsisSem(VOID);
extern VOID ItSuiteFuzzCmsisMutex(VOID);
extern VOID ItSuiteFuzzCmsisMq(VOID);
extern VOID ItSuiteFuzzCmsisEvflag(VOID);
extern VOID ItSuiteFuzzCmsisTimer(VOID);
extern VOID ItSuiteFuzzCmsisKernel(VOID);
extern VOID ItSuiteFuzzPosixPthread(VOID);
extern VOID ItSuiteFuzzPosixSem(VOID);
extern VOID ItSuiteFuzzPosixMqueue(VOID);
extern VOID ItSuiteFuzzPosixMutex(VOID);
extern VOID ItSuiteFuzzPosixCond(VOID);
extern VOID ItSuiteFuzzPosixRwlock(VOID);
extern VOID ItSuiteFuzzPosixAttr(VOID);
extern VOID ItSuiteFuzzPosixTime(VOID);

VOID ItSuiteFuzzAll(VOID)
{
    ItSuiteFuzzLosTask();
    ItSuiteFuzzLosSem();
    ItSuiteFuzzLosQueue();
    ItSuiteFuzzLosMux();
    ItSuiteFuzzLosEvent();
    ItSuiteFuzzLosSwtmr();
    ItSuiteFuzzLosMem();
    ItSuiteFuzzLosMisc();
    ItSuiteFuzzCmsisThread();
    ItSuiteFuzzCmsisSem();
    ItSuiteFuzzCmsisMutex();
    ItSuiteFuzzCmsisMq();
    ItSuiteFuzzCmsisEvflag();
    ItSuiteFuzzCmsisTimer();
    ItSuiteFuzzCmsisKernel();
    ItSuiteFuzzPosixPthread();
    ItSuiteFuzzPosixSem();
    ItSuiteFuzzPosixMqueue();
    ItSuiteFuzzPosixMutex();
    ItSuiteFuzzPosixCond();
    ItSuiteFuzzPosixRwlock();
    ItSuiteFuzzPosixAttr();
    ItSuiteFuzzPosixTime();
}
