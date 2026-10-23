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
 *
 * net_test.c - user demo for the slave-side real-time NIC (RK3588
 * GMAC1 / ETH2, YT9214 switch behind the RJ45).
 *
 * Compiled in via LOSCFG_MCS_NET (see board/BUILD.gn) and launched from
 * main.c. This is the reference usage example for the Net* API - use it
 * as a template for your own protocol task:
 *
 *   1. NetInit with the demo configuration: pure polling RX
 *      (NET_MODE_POLL: no interrupt is registered at all, NetRecv()
 *      reaps the DMA ring directly, so irq_count stays 0). Link policy
 *      is always "master-configured" on this port (the RGMII side runs
 *      fixed 1000M full duplex; see net.h).
 *   2. NetStart (requires the master mcs_net.ko to be loaded first:
 *      the shared control block must be READY).
 *   3. Wait (bounded) for the published link to come up.
 *   4. Forever: send one numbered broadcast test frame per period
 *      (ethertype 0x88B5, payload "NET-DEMO-<n>" - visible in
 *      Wireshark), drain everything received via NetRecv(), and print
 *      a one-line statistics summary once a second.
 *
 * RX verification (the physical RX path needs traffic ON ETH2 - the
 * two RJ45 ports behind the YT9214 switch, NOT the ETH1 debug port):
 *   - self-loopback: patch a cable between the two ETH2 RJ45s (J4<->J5).
 *     Our broadcast frames flood around the loop and come back on the
 *     RGMII side - each TX frame may show up MULTIPLE times (switch
 *     flooding through the physical loop); "rx m" growing means the RX
 *     data path (pins -> MAC -> DMA ring -> Recv) is verified.
 *   - external: attach any traffic source to one ETH2 port (laptop
 *     sending broadcasts / any frames - promisc is on).
 * Every drained frame is classified: m = our 0x88B5 NET-DEMO frames
 * (verified payload prefix, first one dumped in full), o = anything
 * else (switch management / external), bad = runt frames.
 *
 * The demo never stops; NetStop() (quiesce within a bounded timeout)
 * is documented in net.h.
 */

#include <stdint.h>
#include <string.h>
#include "los_compiler.h"
#include "los_debug.h"
#include "los_task.h"
#include "net.h"
#include "gmac1_profile.h"

#define NET_DEMO_PERIOD_MS      100    /* one TX frame + RX drain per period */
#define NET_DEMO_STATS_EVERY    10     /* stats line every 10 periods (1s) */
#define NET_DEMO_LINK_WAIT_MS   8000   /* real ms: PHY wake + autoneg 2-4s plus
                                        * the master's 1s poll cadence */

/* Local Experimental Ethertype 1: does not collide with ECAT 0x88A4 */
#define NET_DEMO_ETHERTYPE      0x88B5

#define DEMO_PAYLOAD_LEN           36
#define DEMO_FRAME_LEN             (14 + DEMO_PAYLOAD_LEN)

/* The one configuration this demo ships with: polled RX (the RGMII side
 * is fixed 1000M FD by hardware - nothing to force). autoneg must be 1:
 * NetInit() rejects autoneg == 0 with NET_ENOTSUP (this port has no
 * forceable PHY behind the MAC; a zero-initialized field would fail). */
static const NetConfig g_demo_cfg = {
    .port_id     = NET_PORT_GMAC1_RK3588,
    .mode        = NET_MODE_POLL,     /* no IRQ at all, Recv() reaps */
    .promisc     = 1,                 /* accept every frame (demo default) */
    .autoneg     = 1,                 /* required by NetInit (no force mode) */
};

static void DemoSendFrame(NetDevice *dev, uint32_t round)
{
    static uint8_t frame[DEMO_FRAME_LEN];
    int ret;

    (VOID)memset_s(frame, DEMO_FRAME_LEN, 0, DEMO_FRAME_LEN);
    for (int i = 0; i < 6; i++) {
        frame[i] = 0xff;                     /* dst broadcast */
    }
    frame[6] = 0x02; frame[7] = 0x11;        /* src (GMAC1_DEFAULT_MAC) */
    frame[8] = 0x22; frame[9] = 0x33;
    frame[10] = 0x44; frame[11] = 0x55;
    frame[12] = NET_DEMO_ETHERTYPE >> 8;
    frame[13] = NET_DEMO_ETHERTYPE & 0xff;
    (VOID)snprintf_s((char *)&frame[14], DEMO_PAYLOAD_LEN,
                     DEMO_PAYLOAD_LEN, "NET-DEMO-%06u", round);

    ret = NetSend(dev, frame, DEMO_FRAME_LEN);
    if (ret != 0) {
        PRINTK("[net] [demo] tx#%u FAILED: %d\n", round, ret);
    }
}

/* per-frame rx classification for the verification output */
typedef struct {
    uint32_t matched;     /* our 0x88B5 NET-DEMO frames (looped back or echoed) */
    uint32_t other;       /* any other ethertype (switch mgmt / external traffic) */
    uint32_t bad;         /* runts shorter than an ethernet header */
} NetDemoRx;

static void DemoDrainRx(NetDevice *dev, uint8_t *rxb, uint32_t round, NetDemoRx *rx)
{
    uint32_t flen = 0;
    int ret;

    for (;;) {
        ret = NetRecv(dev, rxb, NET_FRAME_SIZE, &flen);
        if (ret == NET_EAGAIN) {
            return;                     /* ring drained */
        }
        if (ret != 0) {
            PRINTK("[net] [demo] rx err: %d\n", ret);
            return;
        }
        if (flen < 14u) {
            rx->bad++;
            continue;
        }
        uint16_t etype = (uint16_t)((rxb[12] << 8) | rxb[13]);
        if ((etype == NET_DEMO_ETHERTYPE) &&
            (flen >= 14u + 9u) &&
            (memcmp(&rxb[14], "NET-DEMO-", 9) == 0)) {
            rx->matched++;
            if (rx->matched == 1) {
                /* payload "NET-DEMO-%06u": digits start at offset 23 */
                uint32_t frx_round = 0;
                for (uint32_t k = 23; (k < 29u) && (k < flen); k++) {
                    if ((rxb[k] < '0') || (rxb[k] > '9')) {
                        break;
                    }
                    frx_round = frx_round * 10u + (uint32_t)(rxb[k] - '0');
                }
                PRINTK("[net] [demo] FIRST RX FRAME (%u bytes, tx round #%u "
                       "was #%u): ETH2 RX path VERIFIED\n",
                       flen, round, frx_round);
                PRINTK("[net] [demo]   dst %02x:%02x:%02x:%02x:%02x:%02x "
                       "src %02x:%02x:%02x:%02x:%02x:%02x etype 0x%04x "
                       "payload \"%.24s\"\n",
                       rxb[0], rxb[1], rxb[2], rxb[3], rxb[4], rxb[5],
                       rxb[6], rxb[7], rxb[8], rxb[9], rxb[10], rxb[11],
                       etype, (char *)&rxb[14]);
            }
        } else {
            rx->other++;
            if (rx->other <= 3) {
                PRINTK("[net] [demo] rx frame: %u bytes etype 0x%04x "
                       "(not our demo frame)\n", flen, etype);
            }
        }
    }
}

STATIC VOID *NetDemoTask(UINT32 arg)
{
    static uint8_t rxb[NET_FRAME_SIZE];
    NetDevice *dev = NULL;
    NetLinkInfo li = {0};
    NetDemoRx rx = {0};
    uint32_t round = 0;
    int ret;

    (VOID)arg;
    PRINTK("[net] task enter\n");

    /* 1-2. configure and start (fails if the master ctrl block is not
     * READY: load mcs_net.ko on the Linux side first) */
    ret = NetInit(&g_demo_cfg, &dev);
    if (ret != 0) {
        PRINTK("[net] [demo] Init failed: %d\n", ret);
        return NULL;
    }
    ret = NetStart(dev);
    if (ret != 0) {
        PRINTK("[net] [demo] Start failed: %d (master mcs_net.ko loaded?)\n", ret);
        return NULL;
    }
    PRINTK("[net] [demo] up (fixed 1000M RGMII, poll mode)\n");

    /* 3. bounded wait for the published link (the master samples the
     * YT9214 copper side, or publishes the fixed 1000M when the switch
     * runs unmanaged) */
    for (uint32_t waited = 0; waited < NET_DEMO_LINK_WAIT_MS;
         waited += NET_DEMO_PERIOD_MS) {
        (VOID)NetGetLinkInfo(dev, &li);
        if (li.link_up) {
            break;
        }
        /* liveness + master-published ctrl values while waiting (the
         * master clears the PHY power-down bit after unbind; autoneg
         * completes 2-4s later) */
        PRINTK("[net] lw %ums: up=%u v=%u sp=%u\n",
               waited, li.link_up, li.valid, li.speed_mbps);
        (VOID)LOS_TaskDelay(NET_DEMO_PERIOD_MS); /* tick = 1ms: real 100ms */
    }
    if (li.link_up) {
        PRINTK("[net] [demo] link up: %uM %s\n",
               li.speed_mbps, li.full_duplex ? "FD" : "HD");
    } else {
        PRINTK("[net] [demo] WARNING: link still down after %dms "
               "(cable seated? switch up?)\n", NET_DEMO_LINK_WAIT_MS);
    }

    /* 4. traffic loop - replace this body with your protocol */
    for (;;) {
        round++;
        DemoSendFrame(dev, round);
        DemoDrainRx(dev, rxb, round, &rx);

        if ((round == 1u) || ((round % NET_DEMO_STATS_EVERY) == 0)) {
            NetStats st = {0};
            (VOID)NetGetStats(dev, &st);
            (VOID)NetGetLinkInfo(dev, &li);
            PRINTK("[net] [demo] r%u tx a%u/c%u/e%u/b%u rx d%u/m%u/o%u/bad%u "
                   "e%u/drop%u irq%u dma%u link=%d/%uM/%s\n",
                   round, st.tx_accepted, st.tx_completed, st.tx_errors,
                   st.tx_busy, st.rx_delivered, rx.matched, rx.other, rx.bad,
                   st.rx_errors, st.rx_dropped,
                   st.irq_count, st.dma_errors, li.link_up, li.speed_mbps,
                   li.full_duplex ? "FD" : "HD");
        }
        (VOID)LOS_TaskDelay(NET_DEMO_PERIOD_MS); /* tick = 1ms: real 100ms */
    }
    /* not reached: the demo runs forever; see net.h for NetStop */
}

UINT32 NetTestTaskInit(VOID)
{
    UINT32 taskId;
    TSK_INIT_PARAM_S param = {0};

    param.pfnTaskEntry = (TSK_ENTRY_FUNC)NetDemoTask;
    param.usTaskPrio = 22;
    param.uwStackSize = 0x2000;
    param.pcName = "net_demo";
    return LOS_TaskCreate(&taskId, &param);
}
