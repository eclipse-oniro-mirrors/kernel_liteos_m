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

/* it_driver_can.h — can 驱动域自持头（2026-11-14 自 It_mcs_drv.h 拆分，域间解耦） */
#ifndef _IT_DRIVER_CAN_H_
#define _IT_DRIVER_CAN_H_

#include "osTest.h"
#include <stdint.h>
#include <string.h>
#include "los_compiler.h"
#include "los_debug.h"
#include "los_task.h"

#ifdef LOSCFG_MCS_CAN
#include "can.h"

/* CanRecv is non-blocking (ready-queue semantics): after CanSend the
 * loopback frame still needs a TX-complete -> RXSTX -> fifo/IRQ hop,
 * so poll CanRecv with short delays until the frame lands. */
static inline int ItDriverCanRecvWait(canDevice *dev, canFrame *frame, uint32_t timeoutMs)
{
    int ret;
    uint32_t waited = 0;

    while ((ret = CanRecv(dev, frame)) == CAN_EAGAIN) {
        if (waited >= timeoutMs) {
            break;
        }
        LOS_TaskDelay(2);
        waited += 4; /* tick 500Hz: one delay = 4ms */
    }
    return ret;
}

static inline void ItDriverCanFillFrame(canFrame *f, uint32_t id, uint8_t flags, uint8_t len)
{
    uint32_t i;
    (void)memset(f, 0, sizeof(*f));
    f->id = id;
    f->flags = flags;
    f->len = len;
    for (i = 0; i < len && i < CAN_FD_MAX_LEN; i++) {
        f->data[i] = (uint8_t)(i + 1);
    }
}

/* 域聚合 */
VOID ItDriverCanSuite(VOID);
/* 用例（api 001-005 / 功能 201-221 / 压力 301-304） */
VOID ItDriverCan001(VOID);
VOID ItDriverCan002(VOID);
VOID ItDriverCan003(VOID);
VOID ItDriverCan004(VOID);
VOID ItDriverCan005(VOID);
VOID ItDriverCan201(VOID);
VOID ItDriverCan202(VOID);
VOID ItDriverCan203(VOID);
VOID ItDriverCan204(VOID);
VOID ItDriverCan205(VOID);
VOID ItDriverCan206(VOID);
VOID ItDriverCan207(VOID);
VOID ItDriverCan208(VOID);
VOID ItDriverCan209(VOID);
VOID ItDriverCan210(VOID);
VOID ItDriverCan211(VOID);
VOID ItDriverCan212(VOID);
VOID ItDriverCan213(VOID);
VOID ItDriverCan214(VOID);
VOID ItDriverCan215(VOID);
VOID ItDriverCan216(VOID);
VOID ItDriverCan217(VOID);
VOID ItDriverCan218(VOID);
VOID ItDriverCan219(VOID);
VOID ItDriverCan220(VOID);
VOID ItDriverCan221(VOID);
VOID ItDriverCan301(VOID);
VOID ItDriverCan302(VOID);
VOID ItDriverCan303(VOID);
VOID ItDriverCan304(VOID);
#endif /* LOSCFG_MCS_CAN */
#endif
