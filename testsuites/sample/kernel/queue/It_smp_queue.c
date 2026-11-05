/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above notice, this list of
 * conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above notice, this list of
 * conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor that of its contributors may be used
 * to endorse or promote products derived from this software without specific prior
 * written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "It_los_queue.h"


VOID ItSuiteSmpQueue(void)
{
#if (LOS_KERNEL_TEST_FULL == 1)
#ifdef LOSCFG_KERNEL_SMP
    ItSmpLosQueue001();
    ItSmpLosQueue002();
    ItSmpLosQueue003();
    ItSmpLosQueue004();
    ItSmpLosQueue005();
    ItSmpLosQueue006();
    ItSmpLosQueue007();
    ItSmpLosQueue008();
    ItSmpLosQueue009();
    ItSmpLosQueue010();
    ItSmpLosQueue011();
    ItSmpLosQueue012();
    ItSmpLosQueue013();
    ItSmpLosQueue014();
    ItSmpLosQueue015();
    ItSmpLosQueue016();
    ItSmpLosQueue017();
    ItSmpLosQueue018();
    ItSmpLosQueue019();
    ItSmpLosQueue020();
    ItSmpLosQueue021();
    ItSmpLosQueue022();
    ItSmpLosQueue023();
    ItSmpLosQueue024();
    ItSmpLosQueue025();
    ItSmpLosQueue026();
    ItSmpLosQueue027();
    ItSmpLosQueue029();
    ItSmpLosQueue031();
    ItSmpLosQueue032();
#endif
#endif
}
