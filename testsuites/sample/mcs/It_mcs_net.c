/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "It_mcs.h"

VOID ItSuiteMcsNet(VOID)
{
#ifdef LOSCFG_MCS_NET
    ItMcsNet001();
    ItMcsNet002();
    ItMcsNet003();
    ItMcsNet004();
    ItMcsNet005();
    ItMcsNet006();
    ItMcsNet007();
    ItMcsNet008();
    ItMcsNet009();
    ItMcsNet010();
#endif /* LOSCFG_MCS_NET */
}
