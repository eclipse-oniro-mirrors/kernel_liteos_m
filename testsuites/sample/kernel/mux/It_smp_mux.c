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

#include "It_los_mux.h"


UINT32 g_mutexkernelTest;

VOID ItSuiteSmpMux(void)
{
#if (LOS_KERNEL_TEST_FULL == 1)
#ifdef LOSCFG_KERNEL_SMP
    ItSmpLosMux001();
    ItSmpLosMux002();
    ItSmpLosMux003();
    ItSmpLosMux004();
    ItSmpLosMux005();
    ItSmpLosMux006();
    ItSmpLosMux007();
    ItSmpLosMux2001();
    ItSmpLosMux2002();
    ItSmpLosMux2003();
    ItSmpLosMux2004();
    ItSmpLosMux2005();
    ItSmpLosMux2006();
    ItSmpLosMux2007();
    ItSmpLosMux2008();
    ItSmpLosMux2009();
    ItSmpLosMux2010();
    ItSmpLosMux2011();
    ItSmpLosMux2012();
    ItSmpLosMux2013();
    ItSmpLosMux2014();
    ItSmpLosMux2015();
    ItSmpLosMux2016();
    ItSmpLosMux2017();
    ItSmpLosMux2018();
    ItSmpLosMux2021();
    ItSmpLosMux2022();
    ItSmpLosMux2024();
    ItSmpLosMux2025();
    ItSmpLosMux2026();
    ItSmpLosMux2027();
    ItSmpLosMux2028();
    ItSmpLosMux2029();
#endif
#endif
}
