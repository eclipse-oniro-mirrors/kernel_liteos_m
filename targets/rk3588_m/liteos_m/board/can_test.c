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
 * can_test.c - user demo for the slave-side real-time CAN ports
 * (CAN1 + CAN2, one CanDevice per port, concurrently usable) on the
 * RK3588, including CAN-FD phases.
 *
 * Compiled in via LOSCFG_MCS_CAN (see board/BUILD.gn) and launched from
 * main.c. L1 bring-up: everything runs in CAN_WORK_LOOPBACK (internal
 * loopback + self-ack, nothing driven on the pins) so the whole suite
 * needs ZERO external hardware - no transceiver, no second node.
 *
 * Phases (each: Init -> Start -> N verified frames -> Stop):
 *   --- classic (same coverage as the RK3568 suite) ---
 *   1. lb-irq-500k   loopback + interrupt rx, 500kbps, 20 std data frames
 *   2. lb-poll-500k  loopback + polled rx (no irq at all), same traffic
 *   3. lb-hyb-500k   loopback + hybrid rx (ISR reaps ahead, Recv()
 *                    reaps again as backstop), same traffic
 *   4. lb-bd-*       loopback + irq at 125k / 250k / 1M (800k is
 *                    unreachable at a 100MHz bit clock - see can.h)
 *   5. lb-raw-500k   raw bit timing (brp/sjw/ts1/ts2) instead of the
 *                    solver, values equal to the 500k solution
 *   6. lb-frames     std/ext x data/rtr x dlc 0..8 boundary sweep
 *   7. lb-flt-even   acceptance filter: code/mask bank keeps even ids
 *                    only; odd ids must vanish in the hw filter (no
 *                    echo within the ~10ms quiet window), even ids
 *                    echo-verify as usual
 *   --- CAN-FD (fd_mode = 1; RK3588 IP is FD-capable, can.h v2) ---
 *   8. fd-irq-500k-2m  FDF+BRS frames, arbitration 500k / data 2M (the
 *                      default data rate), dlc 15 (64 bytes), irq rx
 *   9. fd-poll-500k-2m same traffic, polled rx
 *  10. fd-nobrs-500k   FDF frames WITHOUT bit-rate switch (data phase
 *                      runs at the arbitration rate)
 *  11. fd-db-5m        data phase at 5M (arbitration 500k)
 *  12. fd-ext-2m       29-bit ids + FDF+BRS
 *  13. fd-dlc-sweep    FDF frames, on-wire dlc sweep 0..15 (payload
 *                      0..64 bytes via the 9..15 encodings)
 *  14. fd-mixed        classic and FD frames interleaved on the same
 *                      fd-mode port (both frame types are legal there)
 *  15. fd-raw-data     raw data-phase timing equal to the 2M solution
 *   --- normal-mode peer phases (real bus: wire a USB-CAN analyzer to
 *       J11 = CAN1 transceiver terminal; WITHOUT a peer these fail and
 *       that is EXPECTED - the verdict is tracked separately so the
 *       loopback suite above still reports clean) ---
 *  16. nm-tx-500k       20 classic frames onto the real bus; Send() == 0
 *                       means the frame hit the pins AND got acked by
 *                       the peer (auto-retry + timeout otherwise). The
 *                       analyzer should show std id 0x100..0x113, 8 data
 *                       bytes data[i] = 0xa0 ^ (i + seq).
 *  17. nm-rx-500k       10 frames from the peer in a 20s window, any id;
 *                       each received frame is printed.
 *  18. fd-nm-tx-500k-2m 10 FD frames (FDF+BRS, 64 bytes), arbitration
 *                       500k / data 2M - the analyzer must be set to
 *                       500k/2M to see them intact.
 *  19. fd-nm-rx-500k-2m 5 FD frames from the peer in a 20s window.
 *   --- CAN2 (optional phases: when the board's dts has no CAN2 node
 *       the master never publishes the CAN2 ctrl block and the phase
 *       is SKIPPED with a line instead of failing) ---
 *  20. lb2/fd2-*       the basics + FD on CAN2
 *
 * Every received frame is verified against what was sent (id, flags,
 * dlc, payload). A phase passes when all frames round-trip and Stop
 * returns 0; the final line "[can] all phases done" means the port is
 * healthy. Replace the phase bodies with your own protocol task.
 */

#include <stdint.h>
#include <string.h>
#include "los_compiler.h"
#include "los_debug.h"
#include "los_task.h"
#include "can.h"
#include "can1_profile.h"

#define CAN_DEMO_SEND_TIMEOUT_US   100000u  /* 100ms per frame, plenty:
                                             * 1 frame @125k ~ 1.1ms */
#define CAN_DEMO_STATS_EVERY       10       /* stats line cadence */
#define CAN_DEMO_SETTLE_TICKS      2        /* 20ms between phases */
#define CAN_DEMO_PEER_BAIL         3        /* consecutive tx timeouts before
                                             * a peer phase gives up (no
                                             * analyzer on J11) */

/* bring-up diagnostics in can_rk3588.c: register snapshot on tx failure */
extern void CanDiagDump(const CanDevice *dev, const char *tag);

/* ------------------------------------------------------------------ */
/* generic-timer backoff (A76 core: no calibrated spin loop)           */
/* ------------------------------------------------------------------ */

static inline uint64_t DemoCntpct(void)
{
    uint64_t v;

    /* CNTVCT, not CNTPCT: the physical-counter read freezes CPU7 (see
     * net_dwmac.c); the virtual counter is the slave's only usable clock. */
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(v));
    return v;
}

static inline uint32_t DemoCntfrq(void)
{
    uint32_t v;

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
    return v;
}

/* busy-wait ~us microseconds off the generic timer (coarse is fine -
 * it only backs off an EAGAIN retry loop) */
static void DemoSpinUs(uint32_t us)
{
    uint64_t deadline = DemoCntpct() +
        ((uint64_t)DemoCntfrq() * (uint64_t)us) / 1000000u;

    while ((int64_t)(DemoCntpct() - deadline) < 0) {
        __asm__ volatile("nop");
    }
}

/* ------------------------------------------------------------------ */
/* phase driver                                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *label;
    CanConfig cfg;
    uint32_t nframes;
    uint32_t ext;          /* 1 = extended ids */
    uint32_t rtr;          /* 1 = remote frames (classic only) */
    uint32_t dlc_sweep;    /* 1 = sweep dlc 0..8 instead of fixed 8 */
    uint32_t fd_dlc_sweep; /* 1 = FDF frames, sweep on-wire dlc 0..15 */
    uint32_t optional;     /* 1 = skip gracefully on CAN_ENODEV (port
                            * not handed over by the master on this
                            * board: no dts node / ko) */
    uint32_t filter_test;  /* 1 = predict per-frame acceptance with the
                            * cfg's code/mask rule; rejected frames must
                            * NEVER surface via Recv() (counted as
                            * filtered, leaks are errors) */
    uint32_t fd;           /* 1 = CAN-FD frames (cfg.fd_mode must be 1) */
    uint32_t brs;          /* 1 = bit-rate switch on the FD frames */
    uint32_t mixed;        /* 1 = alternate classic / FD frames */
    /* --- normal-mode peer phases (real bus, USB-CAN analyzer on J11) --- */
    uint32_t peer;         /* 1 = needs a live bus peer: the verdict is
                            * tracked SEPARATELY (no analyzer attached
                            * is a missing testbed, not a driver bug) */
    uint32_t tx_only;      /* 1 = verdict on TX-complete only: Send()==0
                            * on the real bus means the frame hit the
                            * pins AND was acked; the RXSTX mirror echo
                            * is drained and verified opportunistically
                            * (this silicon may not mirror in normal
                            * mode - that alone is not a failure) */
    uint32_t rx_wait;      /* 1 = receive-only: wait for nframes peer
                            * frames within rx_window_ms, print each
                            * one, never transmit */
    uint32_t rx_window_ms; /* rx_wait window in milliseconds */
} CanPhase;

/* frame style state (kept in tiny statics to keep the phase driver
 * readable; a real protocol task would own its own context) */
static uint32_t g_ext, g_rtr, g_dlc, g_fdf, g_brs;

static void DemoSetFrameStyle(uint32_t ext, uint32_t rtr, uint32_t dlc,
                              uint32_t fdf, uint32_t brs)
{
    g_ext = ext;
    g_rtr = rtr;
    g_dlc = dlc;
    g_fdf = fdf;
    g_brs = brs;
}

static void DemoMakeFrame(CanFrame *f, uint32_t seq)
{
    uint32_t i, len;

    f->flags = 0;
    f->id = g_ext ? (0x10000u + seq) : (0x100u + seq);
    if (g_ext != 0) {
        f->flags |= CAN_FRAME_EFF;
    }
    if (g_rtr != 0) {
        f->flags |= CAN_FRAME_RTR;
    }
    f->dlc = (uint8_t)g_dlc;
    f->reserved = 0;
    if (g_fdf != 0) {
        f->flags |= CAN_FRAME_FDF;
        if (g_brs != 0) {
            f->flags |= CAN_FRAME_BRS;
        }
        len = CanFdDlc2Len(f->dlc);
    } else {
        len = f->dlc;
    }
    for (i = 0; i < len; i++) {
        f->data[i] = (uint8_t)(0xa0u ^ (i + seq));
    }
}

static int DemoFrameEqual(const CanFrame *a, const CanFrame *b)
{
    uint32_t i, len;

    if ((a->id != b->id) || (a->flags != b->flags) || (a->dlc != b->dlc)) {
        return 0;
    }
    /* RTR frames carry no data: the echo's data words are undefined,
     * compare the header only (RTR is classic-only) */
    if ((a->flags & CAN_FRAME_RTR) != 0) {
        return 1;
    }
    len = ((a->flags & CAN_FRAME_FDF) != 0) ? CanFdDlc2Len(a->dlc) : a->dlc;
    for (i = 0; i < len; i++) {
        if (a->data[i] != b->data[i]) {
            return 0;
        }
    }
    return 1;
}

static int DemoRunPhase(const CanPhase *ph)
{
    CanDevice *dev = NULL;
    CanStats st = {0};
    CanBusInfo bi = {0};
    CanFrame tx, rx;
    uint32_t sent = 0, recvd = 0, errors = 0, filtered = 0;
    uint32_t seq = 0, poll, echo_seen, should_pass, use_fd;
    uint32_t diag_dumps = 0;
    uint32_t fail_streak = 0;   /* consecutive tx timeouts (peer bail-out) */
    int ret;

    ret = CanInit(&ph->cfg, &dev);
    if (ret != 0) {
        if ((ph->optional != 0) && (ret == CAN_ENODEV)) {
            /* the master never handed this port over (no dts node /
             * ko on this board) - not a failure, just absent */
            PRINTK("[can] [%s] skipped (no master handover for port %u)\n",
                   ph->label, ph->cfg.port_id);
            return 0;
        }
        PRINTK("[can] [%s] Init failed: %d\n", ph->label, ret);
        return -1;
    }
    ret = CanStart(dev);
    if (ret != 0) {
        PRINTK("[can] [%s] Start failed: %d\n", ph->label, ret);
        return -1;
    }
    PRINTK("[can] [%s] up\n", ph->label);

    if (ph->peer != 0) {
        PRINTK("[can] [%s] PEER phase: wire the USB-CAN analyzer to J11 "
               "(CAN1, %s); failures without it are EXPECTED\n",
               ph->label,
               ph->cfg.fd_mode != 0 ? "arb 500k / data 2M" : "500k");
    }

    if (ph->rx_wait != 0) {
        /* receive-only phase: sit and wait for peer frames. The task
         * YIELDS between polls (10ms tick) so rpmsg & friends keep
         * running for the whole window.
         * The window is timed on the OS tick counter, NOT on
         * DemoCntpct(): CNTVCT was measured to stop advancing during
         * this phase on 2026-10-18 (the 20s window never elapsed and
         * the phase was killed by slave poweroff) - LOS ticks keep
         * running for the whole demo ([tick] hb lines). */
        UINT64 start_tick = LOS_TickCountGet();
        UINT64 wait_ticks = ph->rx_window_ms / 10u;  /* tick = 10ms */
        uint32_t loops = 0;

        PRINTK("[can] [%s] waiting up to %ums for %u peer frame(s) - "
               "send any frames from the analyzer now\n",
               ph->label, ph->rx_window_ms, ph->nframes);
        while (recvd < ph->nframes) {
            ret = CanRecv(dev, &rx);
            if (ret == CAN_EAGAIN) {
                if ((LOS_TickCountGet() - start_tick) >= wait_ticks) {
                    break;          /* window elapsed */
                }
                loops++;
                if ((loops % 100u) == 0u) {
                    /* heartbeat: proves this loop is alive and how the
                     * tick advances (10 loops per second) */
                    PRINTK("[can] [%s] still waiting: %u polls\n",
                           ph->label, loops);
                }
                (VOID)LOS_TaskDelay(1);
                continue;
            }
            if (ret != 0) {
                PRINTK("[can] [%s] rx#%u FAILED: %d\n", ph->label,
                       recvd + 1u, ret);
                errors++;
                break;
            }
            recvd++;
            {
                uint32_t plen = ((rx.flags & CAN_FRAME_FDF) != 0) ?
                    CanFdDlc2Len(rx.dlc) : rx.dlc;
                PRINTK("[can] [%s] got id=0x%x fl=0x%x dlc=%u len=%u "
                       "data %02x %02x %02x %02x\n",
                       ph->label, rx.id, rx.flags, rx.dlc, plen,
                       plen > 0 ? rx.data[0] : 0, plen > 1 ? rx.data[1] : 0,
                       plen > 2 ? rx.data[2] : 0, plen > 3 ? rx.data[3] : 0);
            }
            if ((recvd % CAN_DEMO_STATS_EVERY) == 0) {
                (VOID)CanGetStats(dev, &st);
                PRINTK("[can] [%s] r%u rx d%u/drop%u/e%u irq%u\n",
                       ph->label, recvd, st.rx_delivered, st.rx_dropped,
                       st.rx_errors, st.irq_count);
            }
        }
        ret = CanStop(dev, 1000);
        PRINTK("[can] [%s] Stop: %d (recvd %u/%u, errors %u)\n",
               ph->label, ret, recvd, ph->nframes, errors);
        return (ret == 0 && errors == 0 && recvd == ph->nframes) ? 0 : -1;
    }

    while (sent < ph->nframes) {
        seq++;

        /* per-frame style: the mixed phase alternates classic/FD */
        use_fd = ph->fd;
        if (ph->mixed != 0) {
            use_fd = (seq & 1u) ? 1u : 0u;
        }
        if (ph->fd_dlc_sweep != 0) {
            /* on-wire dlc 0..15 (payload 0..64 via 9..15 encodings) */
            DemoSetFrameStyle(ph->ext, 0, seq % 16u, 1, ph->brs);
        } else if (ph->dlc_sweep != 0) {
            DemoSetFrameStyle(ph->ext, ph->rtr, seq % 9u, use_fd, ph->brs);
        } else if (use_fd != 0) {
            DemoSetFrameStyle(ph->ext, 0, 15u, 1, ph->brs);   /* 64 bytes */
        } else {
            DemoSetFrameStyle(ph->ext, ph->rtr, 8u, 0, 0);
        }
        DemoMakeFrame(&tx, seq);

        /* filter phases: predict acceptance with the same rule the
         * driver programs into the hw ((id & ~mask) == (code & ~mask)).
         * Predicted-reject frames must never surface via Recv(). */
        if (ph->filter_test != 0) {
            should_pass = ((tx.id & ~ph->cfg.filter_mask) ==
                           (ph->cfg.filter_code & ~ph->cfg.filter_mask))
                          ? 1u : 0u;
        } else {
            should_pass = 1;
        }

        ret = CanSend(dev, &tx, CAN_DEMO_SEND_TIMEOUT_US);
        if (ret != 0) {
            PRINTK("[can] [%s] tx#%u FAILED: %d\n", ph->label, seq, ret);
            if (diag_dumps < 3) {
                diag_dumps++;
                CanDiagDump(dev, "tx-fail");
            }
            errors++;
            /* peer phases: on the real bus a TX timeout means no node
             * ACKed the frame - i.e. no analyzer/peer wired to J11.
             * Without this bail-out the "sent < nframes" loop never
             * terminates (failures don't increment sent) and the
             * 100ms-timeout + recover cycle starved the rpmsg echo
             * task (periodic 202us->202ms rtt spikes, 2026-10-18). */
            if (ph->peer != 0) {
                if (++fail_streak >= CAN_DEMO_PEER_BAIL) {
                    PRINTK("[can] [%s] %u consecutive tx failures - no "
                           "peer on the bus, giving up this phase\n",
                           ph->label, fail_streak);
                    break;
                }
            }
            /* back off one tick (10ms) before retrying: a frozen engine
             * turning this loop into a tight retry was measured starving
             * the rpmsg echo task (rtt 27us -> 79us, then FAIL at 139
             * rounds, 2026-10-17). Yielding keeps rpmsg responsive while
             * the per-send recovery path does its job. */
            (VOID)LOS_TaskDelay(1);
            continue;
        }
        fail_streak = 0;
        sent++;

        if (should_pass == 0) {
            /* expect NOTHING back: spin a bounded quiet window (~10ms);
             * any frame here means the filter leaked it through */
            uint32_t got = 0;

            for (poll = 0; poll < 200; poll++) {
                ret = CanRecv(dev, &rx);
                if (ret == CAN_EAGAIN) {
                    DemoSpinUs(50);
                    continue;
                }
                got = 1;
                if (ret == 0) {
                    PRINTK("[can] [%s] flt-leak rx#%u: id=0x%x "
                           "(filter should drop it)\n",
                           ph->label, seq, rx.id);
                } else {
                    PRINTK("[can] [%s] flt-wait rx#%u FAILED: %d\n",
                           ph->label, seq, ret);
                }
                errors++;
                break;
            }
            if (got == 0) {
                filtered++;       /* silence = the filter did its job */
            }
        } else {
            /* drain the loopback echo. The RXSTX fifo write races the
             * TX0_REQ self-clear: right after Send() returns the echo may
             * not be in the fifo yet, so retry Recv on EAGAIN for a bounded
             * window (~10ms worst case) instead of failing the frame. */
            echo_seen = 0;
            for (poll = 0; poll < 200; poll++) {
                ret = CanRecv(dev, &rx);
                if (ret == CAN_EAGAIN) {
                    if (echo_seen != 0) {
                        break;              /* echo verified, fifo empty */
                    }
                    DemoSpinUs(50);
                    continue;
                }
                if (ret != 0) {
                    errors++;
                    break;
                }
                echo_seen = 1;
                if (!DemoFrameEqual(&tx, &rx)) {
                    PRINTK("[can] [%s] rx#%u mismatch: id=0x%x fl=0x%x dlc=%u "
                           "(want id=0x%x fl=0x%x dlc=%u)\n",
                           ph->label, seq, rx.id, rx.flags, rx.dlc,
                           tx.id, tx.flags, tx.dlc);
                    errors++;
                } else {
                    recvd++;
                }
            }
        }

        if ((sent % CAN_DEMO_STATS_EVERY) == 0) {
            (VOID)CanGetStats(dev, &st);
            (VOID)CanGetBusState(dev, &bi);
            PRINTK("[can] [%s] r%u tx a%u/c%u/to%u rx d%u/drop%u/flt%u/dup%u"
                   "/e%u irq%u bus=%u\n",
                   ph->label, sent, st.tx_accepted, st.tx_completed,
                   st.tx_timeouts, st.rx_delivered, st.rx_dropped, filtered,
                   st.rx_dup_suppressed, st.rx_errors, st.irq_count, bi.state);
        }
    }

    ret = CanStop(dev, 1000);
    PRINTK("[can] [%s] Stop: %d (sent %u, echoed %u, filtered %u, errors %u)\n",
           ph->label, ret, sent, recvd, filtered, errors);
    if (ph->tx_only != 0) {
        /* peer-tx verdict: TX-complete IS the proof - on the real bus
         * Send() returning 0 means the frame hit the pins AND was acked
         * (auto-retry would have timed out otherwise). The echo drain
         * above is a bonus: RXSTX may not mirror normal-mode frames on
         * this silicon, which alone is not a failure. */
        return (ret == 0 && errors == 0 && sent == ph->nframes) ? 0 : -1;
    }
    return (ret == 0 && errors == 0 && (recvd + filtered) == sent) ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* phase table                                                         */
/* ------------------------------------------------------------------ */

static const CanPhase g_phases[] = {
    /* --- classic CAN 2.0B (same coverage as the RK3568 suite) --- */
    {
        .label = "lb-irq-500k",
        .cfg = {
            .port_id = CAN_PORT_CAN1_RK3588,
            .mode = CAN_MODE_IRQ,
            .work_mode = CAN_WORK_LOOPBACK,
            .bitrate = 500000,
        },
        .nframes = 20,
    },
    {
        .label = "lb-poll-500k",
        .cfg = {
            .port_id = CAN_PORT_CAN1_RK3588,
            .mode = CAN_MODE_POLL,
            .work_mode = CAN_WORK_LOOPBACK,
            .bitrate = 500000,
        },
        .nframes = 20,
    },
    {
        .label = "lb-hyb-500k",
        .cfg = {
            .port_id = CAN_PORT_CAN1_RK3588,
            .mode = CAN_MODE_HYBRID,
            .work_mode = CAN_WORK_LOOPBACK,
            .bitrate = 500000,
        },
        .nframes = 20,
    },
    {
        .label = "lb-bd-125k",
        .cfg = {
            .port_id = CAN_PORT_CAN1_RK3588,
            .mode = CAN_MODE_IRQ,
            .work_mode = CAN_WORK_LOOPBACK,
            .bitrate = 125000,
        },
        .nframes = 10,
    },
    {
        .label = "lb-bd-250k",
        .cfg = {
            .port_id = CAN_PORT_CAN1_RK3588,
            .mode = CAN_MODE_IRQ,
            .work_mode = CAN_WORK_LOOPBACK,
            .bitrate = 250000,
        },
        .nframes = 10,
    },
    {
        .label = "lb-bd-1m",
        .cfg = {
            .port_id = CAN_PORT_CAN1_RK3588,
            .mode = CAN_MODE_IRQ,
            .work_mode = CAN_WORK_LOOPBACK,
            .bitrate = 1000000,
        },
        .nframes = 10,
    },
    {
        /* raw timing equal to the 500k solution at 100MHz:
         * brp=4, sjw=1, ts1=43, ts2=6 (N=50 tq, SP=88%) */
        .label = "lb-raw-500k",
        .cfg = {
            .port_id = CAN_PORT_CAN1_RK3588,
            .mode = CAN_MODE_IRQ,
            .work_mode = CAN_WORK_LOOPBACK,
            .use_raw_timing = 1,
            .timing = { .brp = 4, .sjw = 1, .ts1 = 43, .ts2 = 6 },
        },
        .nframes = 10,
    },
    {
        .label = "lb-ext-data",
        .cfg = {
            .port_id = CAN_PORT_CAN1_RK3588,
            .mode = CAN_MODE_IRQ,
            .work_mode = CAN_WORK_LOOPBACK,
            .bitrate = 500000,
        },
        .nframes = 10,
        .ext = 1,
    },
    {
        .label = "lb-std-rtr",
        .cfg = {
            .port_id = CAN_PORT_CAN1_RK3588,
            .mode = CAN_MODE_IRQ,
            .work_mode = CAN_WORK_LOOPBACK,
            .bitrate = 500000,
        },
        .nframes = 10,
        .rtr = 1,
    },
    {
        /* dlc boundary sweep 0..8 on standard data frames */
        .label = "lb-dlc-sweep",
        .cfg = {
            .port_id = CAN_PORT_CAN1_RK3588,
            .mode = CAN_MODE_IRQ,
            .work_mode = CAN_WORK_LOOPBACK,
            .bitrate = 500000,
        },
        .nframes = 18,
        .dlc_sweep = 1,
    },
    {
        /* acceptance filter: keep EVEN ids only. DemoMakeFrame() yields
         * id = 0x100 + seq; code bit0 = 0 with mask = ~0x1 makes the
         * (id & ~mask) == (code & ~mask) rule require id bit0 == 0.
         * Odd ids must vanish inside the hw filter (filtered++, a
         * surfacing frame is a leak error), even ids echo-verify. */
        .label = "lb-flt-even",
        .cfg = {
            .port_id = CAN_PORT_CAN1_RK3588,
            .mode = CAN_MODE_IRQ,
            .work_mode = CAN_WORK_LOOPBACK,
            .bitrate = 500000,
            .filter_en = 1,
            .filter_code = 0x100,
            .filter_mask = 0x7fe,
        },
        .nframes = 20,
        .filter_test = 1,
    },
    /* --- CAN-FD (fd_mode = 1) --- */
    {
        /* the default FD profile: arbitration 500k, data 2M, 64-byte
         * FDF+BRS frames, interrupt rx */
        .label = "fd-irq-500k-2m",
        .cfg = {
            .port_id = CAN_PORT_CAN1_RK3588,
            .mode = CAN_MODE_IRQ,
            .work_mode = CAN_WORK_LOOPBACK,
            .bitrate = 500000,
            .fd_mode = 1,
            .data_bitrate = 2000000,
        },
        .nframes = 20,
        .fd = 1,
        .brs = 1,
    },
    {
        .label = "fd-poll-500k-2m",
        .cfg = {
            .port_id = CAN_PORT_CAN1_RK3588,
            .mode = CAN_MODE_POLL,
            .work_mode = CAN_WORK_LOOPBACK,
            .bitrate = 500000,
            .fd_mode = 1,
            .data_bitrate = 2000000,
        },
        .nframes = 20,
        .fd = 1,
        .brs = 1,
    },
    {
        /* FD frames WITHOUT bit-rate switch: the data phase runs at the
         * arbitration rate (exercises TXFIC FDF without BRS) */
        .label = "fd-nobrs-500k",
        .cfg = {
            .port_id = CAN_PORT_CAN1_RK3588,
            .mode = CAN_MODE_IRQ,
            .work_mode = CAN_WORK_LOOPBACK,
            .bitrate = 500000,
            .fd_mode = 1,
            .data_bitrate = 500000,
        },
        .nframes = 10,
        .fd = 1,
    },
    {
        /* 5M data phase: the fastest commonly-used rate reachable at a
         * 100MHz bit clock with an even prescaler (4M/8M are not) */
        .label = "fd-db-5m",
        .cfg = {
            .port_id = CAN_PORT_CAN1_RK3588,
            .mode = CAN_MODE_IRQ,
            .work_mode = CAN_WORK_LOOPBACK,
            .bitrate = 500000,
            .fd_mode = 1,
            .data_bitrate = 5000000,
        },
        .nframes = 10,
        .fd = 1,
        .brs = 1,
    },
    {
        .label = "fd-ext-2m",
        .cfg = {
            .port_id = CAN_PORT_CAN1_RK3588,
            .mode = CAN_MODE_IRQ,
            .work_mode = CAN_WORK_LOOPBACK,
            .bitrate = 500000,
            .fd_mode = 1,
            .data_bitrate = 2000000,
        },
        .nframes = 10,
        .ext = 1,
        .fd = 1,
        .brs = 1,
    },
    {
        /* on-wire dlc sweep 0..15 on FDF frames: payload 0..64 bytes
         * through the 12/16/20/24/32/48/64 encodings */
        .label = "fd-dlc-sweep",
        .cfg = {
            .port_id = CAN_PORT_CAN1_RK3588,
            .mode = CAN_MODE_IRQ,
            .work_mode = CAN_WORK_LOOPBACK,
            .bitrate = 500000,
            .fd_mode = 1,
            .data_bitrate = 2000000,
        },
        .nframes = 32,
        .fd_dlc_sweep = 1,
        .brs = 1,
    },
    {
        /* classic and FD frames interleaved on the same fd-mode port */
        .label = "fd-mixed",
        .cfg = {
            .port_id = CAN_PORT_CAN1_RK3588,
            .mode = CAN_MODE_IRQ,
            .work_mode = CAN_WORK_LOOPBACK,
            .bitrate = 500000,
            .fd_mode = 1,
            .data_bitrate = 2000000,
        },
        .nframes = 20,
        .fd = 1,
        .brs = 1,
        .mixed = 1,
    },
    {
        /* raw data-phase timing equal to the 2M solution at 100MHz:
         * brp=2, sjw=1, ts1=18, ts2=6 (N=25 tq, SP=76%) */
        .label = "fd-raw-data",
        .cfg = {
            .port_id = CAN_PORT_CAN1_RK3588,
            .mode = CAN_MODE_IRQ,
            .work_mode = CAN_WORK_LOOPBACK,
            .bitrate = 500000,
            .fd_mode = 1,
            .use_raw_data_timing = 1,
            .data_timing = { .brp = 2, .sjw = 1, .ts1 = 18, .ts2 = 6 },
        },
        .nframes = 10,
        .fd = 1,
        .brs = 1,
    },
    /* --- normal-mode peer phases (real bus: USB-CAN analyzer on J11).
     * Verdict tracked separately in CanDemoTask - without the analyzer
     * these fail and the suite still reports the loopback part clean. --- */
    {
        .label = "nm-tx-500k",
        .cfg = {
            .port_id = CAN_PORT_CAN1_RK3588,
            .mode = CAN_MODE_IRQ,
            .work_mode = CAN_WORK_NORMAL,
            .bitrate = 500000,
        },
        .nframes = 20,
        .peer = 1,
        .tx_only = 1,
    },
    {
        .label = "nm-rx-500k",
        .cfg = {
            .port_id = CAN_PORT_CAN1_RK3588,
            .mode = CAN_MODE_IRQ,
            .work_mode = CAN_WORK_NORMAL,
            .bitrate = 500000,
        },
        .nframes = 10,
        .peer = 1,
        .rx_wait = 1,
        /* 8s window (not 20s): the CAN suite must finish well inside the
         * ~41s the master rpmsg_demo needs for 100000 pings - the demo
         * powers the slave off the moment its own pings complete, and a
         * 20s window left the final summary lines truncated (2026-10-18).
         * 8s is still plenty for an analyzer to push 10 frames. */
        .rx_window_ms = 8000,
    },
    {
        .label = "fd-nm-tx-500k-2m",
        .cfg = {
            .port_id = CAN_PORT_CAN1_RK3588,
            .mode = CAN_MODE_IRQ,
            .work_mode = CAN_WORK_NORMAL,
            .bitrate = 500000,
            .fd_mode = 1,
            .data_bitrate = 2000000,
        },
        .nframes = 10,
        .fd = 1,
        .brs = 1,
        .peer = 1,
        .tx_only = 1,
    },
    {
        .label = "fd-nm-rx-500k-2m",
        .cfg = {
            .port_id = CAN_PORT_CAN1_RK3588,
            .mode = CAN_MODE_IRQ,
            .work_mode = CAN_WORK_NORMAL,
            .bitrate = 500000,
            .fd_mode = 1,
            .data_bitrate = 2000000,
        },
        .nframes = 5,
        .peer = 1,
        .rx_wait = 1,
        .rx_window_ms = 8000,      /* see nm-rx: fit inside the rpmsg run */
    },
    /* --- CAN2 (optional: skipped when the board dts/ko has no CAN2) --- */
    {
        .label = "lb2-irq-500k",
        .cfg = {
            .port_id = CAN_PORT_CAN2_RK3588,
            .mode = CAN_MODE_IRQ,
            .work_mode = CAN_WORK_LOOPBACK,
            .bitrate = 500000,
        },
        .nframes = 20,
        .optional = 1,
    },
    {
        .label = "lb2-poll-500k",
        .cfg = {
            .port_id = CAN_PORT_CAN2_RK3588,
            .mode = CAN_MODE_POLL,
            .work_mode = CAN_WORK_LOOPBACK,
            .bitrate = 500000,
        },
        .nframes = 20,
        .optional = 1,
    },
    {
        .label = "fd2-irq-500k-2m",
        .cfg = {
            .port_id = CAN_PORT_CAN2_RK3588,
            .mode = CAN_MODE_IRQ,
            .work_mode = CAN_WORK_LOOPBACK,
            .bitrate = 500000,
            .fd_mode = 1,
            .data_bitrate = 2000000,
        },
        .nframes = 20,
        .fd = 1,
        .brs = 1,
        .optional = 1,
    },
};

#define CAN_PHASE_NUM (sizeof(g_phases) / sizeof(g_phases[0]))

/* ------------------------------------------------------------------ */
/* task                                                                */
/* ------------------------------------------------------------------ */

STATIC VOID *CanDemoTask(UINT32 arg)
{
    uint32_t i;
    int failed = 0;
    int peer_failed = 0;

    (VOID)arg;
    PRINTK("[can] task enter\n");

    for (i = 0; i < CAN_PHASE_NUM; i++) {
        if (DemoRunPhase(&g_phases[i]) != 0) {
            if (g_phases[i].peer != 0) {
                peer_failed++;
            } else {
                failed++;
            }
        }
        (VOID)LOS_TaskDelay(CAN_DEMO_SETTLE_TICKS);
    }

    PRINTK("[can] all phases done%s\n",
           failed == 0 ? "" : " (WITH FAILURES)");
    if (peer_failed != 0) {
        PRINTK("[can] %u peer phase(s) failed - EXPECTED without the "
               "USB-CAN analyzer wired to J11 (arb 500k / data 2M)\n",
               peer_failed);
    }
    return NULL;
}

UINT32 CanTestTaskInit(VOID)
{
    UINT32 taskId;
    TSK_INIT_PARAM_S param = {0};

    param.pfnTaskEntry = (TSK_ENTRY_FUNC)CanDemoTask;
    param.usTaskPrio = 22;
    param.uwStackSize = 0x2000;
    param.pcName = "can_demo";
    return LOS_TaskCreate(&taskId, &param);
}
