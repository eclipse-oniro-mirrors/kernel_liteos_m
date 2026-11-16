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

/* it_driver_net.h — net 驱动域自持头（2026-11-14 自 It_mcs_drv.h 拆分，域间解耦） */
#ifndef _IT_DRIVER_NET_H_
#define _IT_DRIVER_NET_H_

#include "osTest.h"
#include <stdint.h>
#include <string.h>
#include "los_compiler.h"
#include "los_debug.h"
#include "los_task.h"

#ifdef LOSCFG_MCS_NET
#include "net.h"

/* autoneg default config. NOTE: netConfig zero-filled means autoneg=0 +
 * speedMbps=0 which NetInit rejects with -EINVAL; autoneg must be set. */
static inline void ItDriverNetCfgAutoneg(netConfig *cfg)
{
    (void)memset(cfg, 0, sizeof(*cfg));
    cfg->portId = NET_PORT_GMAC1_RK3568;
    cfg->mode = NET_MODE_IRQ;
    cfg->autoneg = 1;
    cfg->promisc = 1;
}

/* build a complete ethernet frame (dst broadcast, src local admin,
 * ethertype 0x88AB test protocol), len must be 14..1514 */
static inline void ItDriverNetFillFrame(uint8_t *frame, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i++) {
        frame[i] = (uint8_t)(i + 1);
    }
    frame[0] = 0xFF; frame[1] = 0xFF; frame[2] = 0xFF;
    frame[3] = 0xFF; frame[4] = 0xFF; frame[5] = 0xFF;
    frame[6] = 0x02; frame[7] = 0x11; frame[8] = 0x22;
    frame[9] = 0x33; frame[10] = 0x44; frame[11] = 0x55;
    frame[12] = 0x88; frame[13] = 0xAB;
}

/* 域聚合 */
VOID ItDriverNetSuite(VOID);
/* 用例（api 001-005 / 功能 201-214 / 压力 301-303） */
VOID ItDriverNet001(VOID);
VOID ItDriverNet002(VOID);
VOID ItDriverNet003(VOID);
VOID ItDriverNet004(VOID);
VOID ItDriverNet005(VOID);
VOID ItDriverNet201(VOID);
VOID ItDriverNet202(VOID);
VOID ItDriverNet203(VOID);
VOID ItDriverNet204(VOID);
VOID ItDriverNet205(VOID);
VOID ItDriverNet206(VOID);
VOID ItDriverNet207(VOID);
VOID ItDriverNet208(VOID);
VOID ItDriverNet209(VOID);
VOID ItDriverNet210(VOID);
VOID ItDriverNet211(VOID);
VOID ItDriverNet212(VOID);
VOID ItDriverNet213(VOID);
VOID ItDriverNet214(VOID);
VOID ItDriverNet301(VOID);
VOID ItDriverNet302(VOID);
VOID ItDriverNet303(VOID);
#endif /* LOSCFG_MCS_NET */
#endif
