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
#ifndef _IT_MCS_H_
#define _IT_MCS_H_

#include "osTest.h"
#include <stdint.h>
#include <string.h>
#include "los_compiler.h"
#include "los_debug.h"
#include "los_task.h"

#ifdef LOSCFG_MCS_CAN
#include "can.h"
#endif
#ifdef LOSCFG_MCS_NET
#include "net.h"
#endif
#ifdef LOSCFG_MCS_RPC
#include "mica/service.h"
#include "mica/rpc.h"
#include "mica/rpc_proto.h"

/* np_* socket proxy API, implemented in board netproxy_test.c */
int np_socket(int domain, int type, int protocol);
int np_connect(int fd, const uint8_t *addr, uint32_t addrLen);
int np_send(int fd, const void *buf, uint32_t len, int flags);
int np_recv(int fd, void *buf, uint32_t cap, int flags, int timeoutMs);
int np_sendto(int fd, const void *buf, uint32_t len, int flags,
              const uint8_t *dest, uint32_t addrLen);
int np_recvfrom(int fd, void *buf, uint32_t cap, int flags,
                int timeoutMs, uint8_t *from, uint32_t *fromLen);
int np_close(int fd);

/* sockaddr_in wire layout used by np_* (see netproxy_test.c
 * NpBuildSockaddrIn): 16-byte buffer, [0]=family(2), [2..3]=port BE,
 * [4..7]=ip BE, [8..15]=0 (the master executes real BSD socket calls,
 * so the full sockaddr_in length is required) */
#define IT_NP_SOCKADDR_LEN 16
static inline void ItNpBuildAddr(uint8_t *out, uint8_t ip0, uint8_t ip1,
                                 uint8_t ip2, uint8_t ip3, uint16_t port)
{
    (void)memset(out, 0, IT_NP_SOCKADDR_LEN);
    out[0] = 0x02;
    out[2] = (uint8_t)(port >> 8);
    out[3] = (uint8_t)(port & 0xff);
    out[4] = ip0;
    out[5] = ip1;
    out[6] = ip2;
    out[7] = ip3;
}

/* echo server used by ItMcsNp006/007 (PRECONDITION): a host reachable
 * from the master eth0 segment running a UDP echo on 9000 and a TCP
 * echo on 9001 (same bench target as the netproxy demo). default: the
 * directly attached PC 192.168.100.1 */
#define IT_NP_ECHO_IP0 192
#define IT_NP_ECHO_IP1 168
#define IT_NP_ECHO_IP2 100
#define IT_NP_ECHO_IP3 1
#define IT_NP_ECHO_PORT_UDP 9000
#define IT_NP_ECHO_PORT_TCP 9001
#endif

#ifdef LOSCFG_MCS_CAN
/* CanRecv is non-blocking (ready-queue semantics): after CanSend the
 * loopback frame still needs a TX-complete -> RXSTX -> fifo/IRQ hop,
 * so poll CanRecv with short delays until the frame lands. */
static inline int ItMcsCanRecvWait(canDevice *dev, canFrame *frame, uint32_t timeoutMs)
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

static inline void ItMcsCanFillFrame(canFrame *f, uint32_t id, uint8_t flags, uint8_t len)
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
#endif

#ifdef LOSCFG_MCS_NET
/* autoneg default config. NOTE: netConfig zero-filled means autoneg=0 +
 * speedMbps=0 which NetInit rejects with -EINVAL; autoneg must be set. */
static inline void ItMcsNetCfgAutoneg(netConfig *cfg)
{
    (void)memset(cfg, 0, sizeof(*cfg));
    cfg->portId = NET_PORT_GMAC1_RK3568;
    cfg->mode = NET_MODE_IRQ;
    cfg->autoneg = 1;
    cfg->promisc = 1;
}

/* build a complete ethernet frame (dst broadcast, src local admin,
 * ethertype 0x88AB test protocol), len must be 14..1514 */
static inline void ItMcsNetFillFrame(uint8_t *frame, uint32_t len)
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
#endif


#ifdef LOSCFG_MCS_TIMER
#include "timer.h"

#define IT_TIMER_US_PER_TICK \
    (1000000ULL / LOSCFG_BASE_CORE_TICK_PER_SECOND)

/* irq-mode expiry recorder: the callback runs in ISR context */
typedef struct {
    timerDevice *dev;
    volatile uint32_t count;
} ItMcsTimerCtx;

static inline void ItMcsTimerCb(void *arg)
{
    ItMcsTimerCtx *c = (ItMcsTimerCtx *)arg;

    c->count++;
}

/* wait until ctx->count reaches want or timeoutMs elapses; 0 on hit */
static inline int ItMcsTimerWaitCount(ItMcsTimerCtx *c, uint32_t want,
                                      uint32_t timeoutMs)
{
    uint32_t waited = 0;

    while (c->count < want) {
        if (waited >= timeoutMs) {
            return -1;
        }
        LOS_TaskDelay(1);
        waited += (uint32_t)(IT_TIMER_US_PER_TICK / 1000ULL) + 1;
    }
    return 0;
}
#endif

/* suite aggregators */
VOID ItSuiteMcsCan(VOID);
VOID ItSuiteMcsNet(VOID);
VOID ItSuiteMcsRpc(VOID);
VOID ItSuiteMcsNp(VOID);
VOID ItSuiteMcsSvc(VOID);
VOID ItSuiteMcsTimer(VOID);

#ifdef LOSCFG_MCS_CAN
VOID ItMcsCan001(VOID); VOID ItMcsCan002(VOID); VOID ItMcsCan003(VOID);
VOID ItMcsCan004(VOID); VOID ItMcsCan005(VOID); VOID ItMcsCan006(VOID);
VOID ItMcsCan007(VOID); VOID ItMcsCan008(VOID); VOID ItMcsCan009(VOID);
VOID ItMcsCan010(VOID); VOID ItMcsCan011(VOID); VOID ItMcsCan012(VOID);
VOID ItMcsCan013(VOID); VOID ItMcsCan014(VOID); VOID ItMcsCan015(VOID);
VOID ItMcsCan016(VOID);
#endif
#ifdef LOSCFG_MCS_NET
VOID ItMcsNet001(VOID); VOID ItMcsNet002(VOID); VOID ItMcsNet003(VOID);
VOID ItMcsNet004(VOID); VOID ItMcsNet005(VOID); VOID ItMcsNet006(VOID);
VOID ItMcsNet007(VOID); VOID ItMcsNet008(VOID); VOID ItMcsNet009(VOID);
VOID ItMcsNet010(VOID);
#endif
#ifdef LOSCFG_MCS_TIMER
VOID ItMcsTimer001(VOID); VOID ItMcsTimer002(VOID); VOID ItMcsTimer003(VOID);
VOID ItMcsTimer004(VOID); VOID ItMcsTimer005(VOID); VOID ItMcsTimer006(VOID);
#endif
#ifdef LOSCFG_MCS_RPC
VOID ItMcsRpc001(VOID); VOID ItMcsRpc002(VOID); VOID ItMcsRpc003(VOID);
VOID ItMcsRpc004(VOID); VOID ItMcsRpc005(VOID); VOID ItMcsRpc006(VOID);
VOID ItMcsRpc007(VOID);
VOID ItMcsNp001(VOID); VOID ItMcsNp002(VOID); VOID ItMcsNp003(VOID);
VOID ItMcsNp004(VOID); VOID ItMcsNp005(VOID); VOID ItMcsNp006(VOID);
VOID ItMcsNp007(VOID); VOID ItMcsNp008(VOID);
VOID ItMcsSvc001(VOID); VOID ItMcsSvc003(VOID);
VOID ItMcsSvc004(VOID); VOID ItMcsSvc005(VOID); VOID ItMcsSvc006(VOID);
VOID ItMcsSvc007(VOID);
#endif

#endif /* _IT_MCS_H_ */
