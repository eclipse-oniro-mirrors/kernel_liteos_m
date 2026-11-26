/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted under the following conditions:
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "It_extend_cpup.h"

extern VOID ItSmpExtendCpup001(VOID);
extern VOID ItSmpExtendCpup002(VOID);
extern VOID ItSmpExtendCpup003(VOID);
extern VOID ItSmpExtendCpup004(VOID);
extern VOID ItSmpExtendCpup005(VOID);
extern VOID ItSmpExtendCpup007(VOID);
extern VOID ItSmpExtendCpup008(VOID);
extern VOID ItSmpExtendCpup009(VOID);
extern VOID ItSmpExtendCpup010(VOID);
extern VOID ItSmpExtendCpup011(VOID);
extern VOID ItSmpExtendCpup012(VOID);

VOID ItSuiteSmpCpup(void)
{
#if (LOS_KERNEL_TEST_FULL == 1)
#ifdef LOSCFG_KERNEL_SMP
    ItSmpExtendCpup001();
    ItSmpExtendCpup002();
#ifdef LOSCFG_CPUP_INCLUDE_IRQ
    ItSmpExtendCpup003();
    ItSmpExtendCpup004();
#endif
    ItSmpExtendCpup005();
    ItSmpExtendCpup007();
    ItSmpExtendCpup008();
    ItSmpExtendCpup009();
    ItSmpExtendCpup010();
    ItSmpExtendCpup011();
    ItSmpExtendCpup012();
#endif
#endif
}
