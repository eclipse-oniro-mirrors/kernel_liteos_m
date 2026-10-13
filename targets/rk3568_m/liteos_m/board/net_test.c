/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 *
 * net_test.c - user demo for the slave-side real-time NIC.
 *
 * Compiled in via LOSCFG_MCS_NET (see board/BUILD.gn) and launched from
 * main.c. This is the reference usage example for the Net* API - use it
 * as a template for your own protocol task:
 *
 *   1. NetInit with the demo configuration: forced 100M full-duplex
 *      link (autoneg off, PHY BMCR written by the slave) and pure
 *      polling RX (NET_MODE_POLL: no interrupt is registered at all,
 *      NetRecv() reaps the DMA ring directly, so irq_count stays 0).
 *   2. NetStart (requires the master mcs_net.ko to be loaded first:
 *      the shared control block must be READY).
 *   3. Wait (bounded) for the forced link to come up.
 *   4. Forever: send one numbered broadcast test frame per period
 *      (ethertype 0x88B5, payload "NET-DEMO-<n>" - visible in
 *      Wireshark), drain everything received via NetRecv(), and print
 *      a one-line statistics summary once a second.
 *
 * The demo never stops; NetStop() (quiesce within a bounded timeout)
 * is documented in USER_GUIDE.md / net.h.
 *
 * Forced-link caveat (see net.h): a forced link requires the link
 * partner to be forced too (set the peer NIC to 100M full-duplex
 * manually). An autoneg partner parallel-detects and usually lands on
 * half duplex - TX frames still go out, RX may not come back.
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
#define NET_DEMO_LINK_WAIT_MS   10000  /* bounded wait for the forced link */

/* Local Experimental Ethertype 1: does not collide with ECAT 0x88A4 */
#define NET_DEMO_ETHERTYPE      0x88B5

#define DEMO_PAYLOAD_LEN           36
#define DEMO_FRAME_LEN             (14 + DEMO_PAYLOAD_LEN)

/* The one configuration this demo ships with: forced 100M FD + polling. */
static const NetConfig g_demo_cfg = {
    .port_id     = NET_PORT_GMAC1_RK3568,
    .mode        = NET_MODE_POLL,     /* no IRQ at all, Recv() reaps */
    .autoneg     = 0,                 /* force, do not autoneg */
    .speed_mbps  = 100,
    .full_duplex = 1,
    .promisc     = 1,                 /* accept every frame (demo default) */
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

static void DemoDrainRx(NetDevice *dev, uint8_t *rxb, uint32_t *rx_seen)
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
        if (*rx_seen == 0) {
            PRINTK("[net] [demo] first rx frame: %u bytes\n", flen);
            *rx_seen = 1;
        }
    }
}

STATIC VOID *NetDemoTask(UINT32 arg)
{
    static uint8_t rxb[NET_FRAME_SIZE];
    NetDevice *dev = NULL;
    NetLinkInfo li = {0};
    uint32_t rx_seen = 0;
    uint32_t round = 0;
    int ret;

    (VOID)arg;

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
    PRINTK("[net] [demo] up (force 100M FD, poll mode)\n");

    /* 3. bounded wait for the forced link */
    for (uint32_t waited = 0; waited < NET_DEMO_LINK_WAIT_MS;
         waited += NET_DEMO_PERIOD_MS) {
        (VOID)NetGetLinkInfo(dev, &li);
        if (li.link_up) {
            break;
        }
        (VOID)LOS_TaskDelay(NET_DEMO_PERIOD_MS / 10); /* tick = 10ms */
    }
    if (li.link_up) {
        PRINTK("[net] [demo] link up: %uM %s\n",
               li.speed_mbps, li.full_duplex ? "FD" : "HD");
    } else {
        PRINTK("[net] [demo] WARNING: link still down after %dms "
               "(peer NIC forced to 100M FD too?)\n", NET_DEMO_LINK_WAIT_MS);
    }

    /* 4. traffic loop - replace this body with your protocol */
    for (;;) {
        round++;
        DemoSendFrame(dev, round);
        DemoDrainRx(dev, rxb, &rx_seen);

        if ((round % NET_DEMO_STATS_EVERY) == 0) {
            NetStats st = {0};
            (VOID)NetGetStats(dev, &st);
            (VOID)NetGetLinkInfo(dev, &li);
            PRINTK("[net] [demo] r%u tx a%u/c%u/e%u/b%u rx d%u/e%u/drop%u "
                   "irq%u dma%u link=%d/%uM/%s\n",
                   round, st.tx_accepted, st.tx_completed, st.tx_errors,
                   st.tx_busy, st.rx_delivered, st.rx_errors, st.rx_dropped,
                   st.irq_count, st.dma_errors, li.link_up, li.speed_mbps,
                   li.full_duplex ? "FD" : "HD");
        }
        (VOID)LOS_TaskDelay(NET_DEMO_PERIOD_MS / 10); /* tick = 10ms */
    }
    /* not reached: the demo runs forever; see USER_GUIDE.md for NetStop */
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
