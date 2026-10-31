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
 * can_rx_demo.c - slave-side CAN1 receive verification demo.
 *
 * Purpose: verify the slave-core CAN RX path end-to-end against a real
 * bus peer (USB-CANFD analyzer wired to J11 CAN1 H/L). Results are
 * published to a shared block in the CAN1 ctrl page so the Linux
 * master can read them with `devmem` - no console dependency (the
 * slave PRINTK path is not observable from the master on this board).
 *
 * Shared block: CAN1 ctrl page 0x740ff100..0x740ff1ff (Device-mapped
 * in los_mmu.c, same page can_rk3588.c uses for the handover
 * protocol). struct can_ctrl occupies the first 64B; the demo block
 * starts at offset 0x40 = 0x740ff140:
 *
 *   0x740ff140  magic   0x52584431 "RXD1" when the demo is live
 *   0x740ff144  state   1=running 3=CanInit-failed 4=CanStart-failed
 *   0x740ff148  rx_count    frames delivered via CanRecv()
 *   0x740ff14c  rx_errors   Recv() hard errors
 *   0x740ff150  polls       EAGAIN poll cycles (liveness heartbeat)
 *   0x740ff154  last_id     most recent frame id
 *   0x740ff158  last_flags  CAN_FRAME_EFF/RTR/FDF/BRS
 *   0x740ff15c  last_dlc    on-wire DLC of the most recent frame
 *   0x740ff160  last_seq    increments on every received frame
 *   0x740ff164  init_ret / start_ret packed (16b each)
 *   0x740ff168  phase       0=sleeping 1..3 = frame update progress
 *   0x740ff16c..0x740ff1ab  last payload, 64B, little-endian words
 *   0x740ff1ac  swtmr_err   LOS_SwtmrCreate ret, 0 = ok
 *   0x740ff1b0  swtmr_beats +1 every 1s from the swtmr callback:
 *                          OS-tick liveness probe. The slave tick is NOT
 *                          interrupt driven (PPI 27 stuck in GIC Group0,
 *                          swallowed by EL3): idle task's SlaveTickPump()
 *                          polls CNTVCT and fires OsTickHandler. If this
 *                          counter stops while polls keeps moving, the
 *                          idle task starved; if both stop, the core is
 *                          wedged inside an ISR.
 *   0x740ff1b4  swtmr_tick  LOS_TickCountGet() snapshot in the callback
 *
 * Analyzer recipe (classic CAN): 500kbps, std data frame e.g. id 0x123
 * dlc 8 data 11 22 33 44 55 66 77 88 - rx_count must tick up and
 * last_id/data must match. FD frames (arb 500k / data 2M, FDF+BRS)
 * are accepted by the same config; last_flags bit2/bit3 tell them
 * apart.
 */

#include <stdint.h>
#include "los_compiler.h"
#include "los_debug.h"
#include "los_task.h"
#include "los_tick.h"
#include "los_swtmr.h"
#include "can.h"

#define RX_DEMO_BLOCK_PHYS   0x740FF140u
#define RX_DEMO_MAGIC        0x52584431u /* "RXD1" */

#define RX_DEMO_STATE_RUNNING   1u
#define RX_DEMO_STATE_INIT_ERR  3u
#define RX_DEMO_STATE_START_ERR 4u

struct rxdemo_blk {
    volatile uint32_t magic;
    volatile uint32_t state;
    volatile uint32_t rx_count;
    volatile uint32_t rx_errors;
    volatile uint32_t polls;
    volatile uint32_t last_id;
    volatile uint32_t last_flags;
    volatile uint32_t last_dlc;
    volatile uint32_t last_seq;
    volatile uint32_t init_start_ret; /* low16=CanInit ret, high16=CanStart */
    volatile uint32_t phase;           /* task liveness: 0=sleeping,
                                       * 1=got frame, 2=header updated,
                                       * 3=payload updated */
    volatile uint32_t last_data[16];  /* 64B payload, LE words */
    volatile uint32_t swtmr_err;      /* LOS_SwtmrCreate ret, 0 = ok */
    volatile uint32_t swtmr_beats;    /* +1 per second: tick liveness */
    volatile uint32_t swtmr_tick;    /* LOS_TickCountGet() in the cb */
};

static struct rxdemo_blk *const blk = (struct rxdemo_blk *)RX_DEMO_BLOCK_PHYS;

static VOID CanRxDemoSwtmrCb(UINTPTR arg)
{
    (VOID)arg;
    blk->swtmr_beats++;
    blk->swtmr_tick = (uint32_t)LOS_TickCountGet();
}

static VOID CanRxDemoTask(UINT32 arg)
{
    CanConfig cfg = {0};
    CanDevice *dev = NULL;
    CanFrame rx;
    uint32_t i;
    int ret;

    (VOID)arg;

    blk->magic = RX_DEMO_MAGIC;
    blk->state = 0;
    blk->rx_count = 0;
    blk->rx_errors = 0;
    blk->polls = 0;
    blk->last_seq = 0;
    blk->init_start_ret = 0;
    blk->swtmr_err = 0;
    blk->swtmr_beats = 0;
    blk->swtmr_tick = 0;

    /* 1Hz software-timer heartbeat, created BEFORE the CAN bring-up so
     * the tick-liveness probe keeps running even if CanInit fails. The
     * slave OS tick is pumped by the idle task (SlaveTickPump), so this
     * callback only fires while the scheduler + idle task are healthy. */
    {
        UINT32 swtmrId = 0;

        ret = (int)LOS_SwtmrCreate(100, LOS_SWTMR_MODE_PERIOD,
                                   CanRxDemoSwtmrCb, &swtmrId, 0);
        blk->swtmr_err = (uint32_t)ret;
        if (ret == LOS_OK) {
            ret = (int)LOS_SwtmrStart(swtmrId);
            if (ret != LOS_OK) {
                blk->swtmr_err = 0x100u | (uint32_t)ret; /* start failed */
            }
        }
    }

    /* CAN1, IRQ mode re-test (2026-10-31): the driver's CanRxPopOne no
     * longer calls PRINT_ERR on duplicate frames (the prime suspect for
     * the slave freeze: PRINTK from ISR context spins on the shared
     * UART2 LSR forever, wedging the core inside the ISR, which also
     * starves idle -> SlaveTickPump -> the tick dies with it). This
     * build re-enables CAN_MODE_IRQ to confirm the fix end-to-end. The
     * demo now blocks in LOS_TaskDelay on an empty ring - the canonical
     * slave shape: tasks sleep, idle pumps the tick, the ISR reaps the
     * hw fifo into the ready ring. fd_mode=1 keeps classic frames legal
     * AND accepts FDF/BRS frames. */
    cfg.port_id = CAN_PORT_CAN1_RK3588;
    cfg.mode = CAN_MODE_IRQ;
    cfg.work_mode = CAN_WORK_NORMAL;
    cfg.bitrate = 500000;
    cfg.fd_mode = 1;
    cfg.data_bitrate = 2000000;
    cfg.filter_en = 0; /* accept everything */

    ret = CanInit(&cfg, &dev);
    blk->init_start_ret = (uint32_t)(ret & 0xffffu);
    if (ret != 0) {
        blk->state = RX_DEMO_STATE_INIT_ERR;
        return;
    }
    ret = CanStart(dev);
    blk->init_start_ret |= (uint32_t)((uint32_t)ret & 0xffffu) << 16;
    if (ret != 0) {
        blk->state = RX_DEMO_STATE_START_ERR;
        return;
    }
    blk->state = RX_DEMO_STATE_RUNNING;

    for (;;) {
        ret = CanRecv(dev, &rx);
        if (ret == CAN_EAGAIN) {
            blk->phase = 0;
            blk->polls++;
            /* Ring empty -> sleep one tick (10ms). While we sleep the
             * idle task runs and SlaveTickPump() advances the OS tick,
             * so the delay returns normally. This ONLY works if the
             * CAN ISR returns - a wedged ISR would freeze this counter
             * (polls) together with swtmr_beats, which is exactly what
             * this build probes. */
            (VOID)LOS_TaskDelay(1);
            continue;
        }
        if (ret != 0) {
            blk->rx_errors++;
            continue;
        }

        uint32_t plen = ((rx.flags & CAN_FRAME_FDF) != 0) ?
            CanFdDlc2Len(rx.dlc) : rx.dlc;

        blk->phase = 1; /* got frame from CanRecv */

        blk->rx_count++;
        blk->last_id = rx.id;
        blk->last_flags = rx.flags;
        blk->last_dlc = rx.dlc;
        blk->last_seq++;

        blk->phase = 2; /* header fields updated */

        for (i = 0; i < 16u; i++) {
            uint32_t w = 0;
            uint32_t b;
            for (b = 0; b < 4u; b++) {
                uint32_t off = i * 4u + b;
                w |= (off < plen) ? ((uint32_t)rx.data[off] << (8u * b)) : 0u;
            }
            blk->last_data[i] = w;
        }

        blk->phase = 3; /* payload updated - NO PRINTK here: the slave
                         * UartPuts spins on the shared UART2 LSR and
                         * wedges the task after the first frame
                         * (suspected 2026-10-30). */
    }
}

UINT32 CanRxDemoTaskInit(VOID)
{
    UINT32 taskId;
    TSK_INIT_PARAM_S param = {0};

    param.pfnTaskEntry = (TSK_ENTRY_FUNC)CanRxDemoTask;
    param.usTaskPrio = 22;
    param.uwStackSize = 0x2000;
    param.pcName = "can_rxdemo";
    return LOS_TaskCreate(&taskId, &param);
}
