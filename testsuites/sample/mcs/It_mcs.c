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

#ifdef LOSCFG_MCS_RPC
/* mica services come up asynchronously in the mica task; wait (bounded)
 * before running the rpc/np/svc suites so they observe a ready endpoint */
static void ItMcsWaitServicesReady(void)
{
    int i;
    for (i = 0; i < 1200; i++) { /* up to 240 s at 500 hz (0.2 s per iter) */
        if (mica_service_is_ready(MICA_SERVICE_RPC) &&
            mica_service_is_ready(MICA_SERVICE_TTY) &&
            mica_service_is_ready(MICA_SERVICE_UMT)) {
            return;
        }
        LOS_TaskDelay(100); /* 0.2 s at 500 hz */
    }
    dprintf("[mcs] services not ready after wait, rpc cases may fail\n");
}
#endif

VOID ItSuiteMcs(VOID)
{
#ifdef LOSCFG_MCS_CAN
    ItSuiteMcsCan();
#endif
#ifdef LOSCFG_MCS_NET
    ItSuiteMcsNet();
#endif
#ifdef LOSCFG_MCS_RPC
    ItMcsWaitServicesReady();
    ItSuiteMcsSvc();
    ItSuiteMcsRpc();
    ItSuiteMcsNp();
#endif
}
