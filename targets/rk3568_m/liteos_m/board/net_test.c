/*
 * copyright (c) 2026 huawei device co., ltd.
 *
 * net_test.c - user demo for the slave-side real-time NIC.
 *
 * compiled in via LOSCFG_MCS_NET (board/BUILD.gn), launched from main.c.
 * reference usage of the net* API (use it as a template for your own
 * protocol task):
 *   1. NetInit: forced 100M full-duplex (autoneg off, PHY BMCR written by
 *      the slave) + pure polling RX (NET_MODE_POLL: no IRQ registered,
 *      NetRecv() reaps the DMA ring directly, irqCount stays 0).
 *   2. NetStart (requires mcs_net.ko loaded first: the shared control
 *      block must be READY).
 *   3. bounded wait for the forced link.
 *   4. forever: one numbered broadcast test frame per period (ethertype
 *      0x88B5, payload "NET-DEMO-<n>" - visible in wireshark), drain all
 *      received frames via NetRecv(), print a stats line once a second.
 * the demo never stops; NetStop() is documented in USER_GUIDE.md / net.h.
 * forced-link caveat (net.h): the link partner must be forced too (peer
 * NIC set to 100M full-duplex manually) - an autoneg partner
 * parallel-detects to half duplex: TX goes out, RX may not come back.
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

/* local experimental ethertype 1: does not collide with ECAT 0x88A4 */
#define NET_DEMO_ETHERTYPE      0x88B5

#define DEMO_PAYLOAD_LEN           36
#define DEMO_FRAME_LEN             (14 + DEMO_PAYLOAD_LEN)

/* the one configuration this demo ships with: forced 100M FD + polling. */
static const netConfig g_demoCfg = {
    .portId     = NET_PORT_GMAC1_RK3568,
    .mode        = NET_MODE_POLL,     /* no IRQ at all, recv() reaps */
    .autoneg     = 0,                 /* force, do not autoneg */
    .speedMbps  = 100,
    .fullDuplex = 1,
    .promisc     = 1,                 /* accept every frame (demo default) */
};

static void DemoSendFrame(netDevice *dev, uint32_t round)
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

static void DemoDrainRx(netDevice *dev, uint8_t *rxBuf, uint32_t *rxSeen)
{
    uint32_t flen = 0;
    int ret;

    for (;;) {
        ret = NetRecv(dev, rxBuf, NET_FRAME_SIZE, &flen);
        if (ret == NET_EAGAIN) {
            return;                     /* ring drained */
        }
        if (ret != 0) {
            PRINTK("[net] [demo] rx err: %d\n", ret);
            return;
        }
        if (*rxSeen == 0) {
            PRINTK("[net] [demo] first rx frame: %u bytes\n", flen);
            *rxSeen = 1;
        }
    }
}

STATIC VOID *NetDemoTask(UINT32 arg)
{
    static uint8_t rxBuf[NET_FRAME_SIZE];
    netDevice *dev = NULL;
    netLinkInfo linkInfo = {0};
    uint32_t rxSeen = 0;
    uint32_t round = 0;
    int ret;

    (VOID)arg;

    /* 1-2. configure and start (PRECONDITION: load mcs_net.ko on the
     * linux side first - see board/DRIVER_GUIDE.md; with the clocks
     * gated the first GMAC MMIO hangs the core) */
    ret = NetInit(&g_demoCfg, &dev);
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
        (VOID)NetGetLinkInfo(dev, &linkInfo);
        if (linkInfo.linkUp) {
            break;
        }
        (VOID)LOS_TaskDelay(NET_DEMO_PERIOD_MS / 10); /* tick = 10ms */
    }
    if (linkInfo.linkUp) {
        PRINTK("[net] [demo] link up: %uM %s\n",
               linkInfo.speedMbps, linkInfo.fullDuplex ? "FD" : "HD");
    } else {
        PRINTK("[net] [demo] WARNING: link still down after %dms "
               "(peer NIC forced to 100M FD too?)\n", NET_DEMO_LINK_WAIT_MS);
    }

    /* 4. traffic loop - replace this body with your protocol */
    for (;;) {
        round++;
        DemoSendFrame(dev, round);
        DemoDrainRx(dev, rxBuf, &rxSeen);

        if ((round % NET_DEMO_STATS_EVERY) == 0) {
            netStats stats = {0};
            (VOID)NetGetStats(dev, &stats);
            (VOID)NetGetLinkInfo(dev, &linkInfo);
            PRINTK("[net] [demo] r%u tx a%u/c%u/e%u/b%u rx d%u/e%u/drop%u "
                   "irq%u dma%u link=%d/%uM/%s\n",
                   round, stats.txAccepted, stats.txCompleted, stats.txErrors,
                   stats.txBusy, stats.rxDelivered, stats.rxErrors, stats.rxDropped,
                   stats.irqCount, stats.dmaErrors, linkInfo.linkUp, linkInfo.speedMbps,
                   linkInfo.fullDuplex ? "FD" : "HD");
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
