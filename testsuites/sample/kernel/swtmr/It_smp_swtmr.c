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

#include "It_los_swtmr.h"


UINT32 g_usSwTmrID;

VOID ItSuiteSmpSwtmr(void)
{
#if (LOS_KERNEL_TEST_FULL == 1)
#ifdef LOSCFG_KERNEL_SMP
    ItSmpLosSwtmr001();
    ItSmpLosSwtmr002();
    ItSmpLosSwtmr003();
    ItSmpLosSwtmr004();
    ItSmpLosSwtmr005();
    ItSmpLosSwtmr006();
    ItSmpLosSwtmr007();
    ItSmpLosSwtmr008();
    ItSmpLosSwtmr009();
    ItSmpLosSwtmr010();
    ItSmpLosSwtmr011();
    ItSmpLosSwtmr012();
    ItSmpLosSwtmr013();
    ItSmpLosSwtmr014();
    ItSmpLosSwtmr015();
    ItSmpLosSwtmr016();
    ItSmpLosSwtmr017();
    ItSmpLosSwtmr018();
    ItSmpLosSwtmr019();
    ItSmpLosSwtmr020();
    ItSmpLosSwtmr021();
    ItSmpLosSwtmr022();
    ItSmpLosSwtmr023();
    ItSmpLosSwtmr024();
    ItSmpLosSwtmr025();
    ItSmpLosSwtmr026();
    ItSmpLosSwtmr027();
    ItSmpLosSwtmr028();
    ItSmpLosSwtmr029();
    ItSmpLosSwtmr030();
    ItSmpLosSwtmr031();
    ItSmpLosSwtmr032();
    ItSmpLosSwtmr033();
    ItSmpLosSwtmr034();
    ItSmpLosSwtmr035();
#endif
#endif
}
