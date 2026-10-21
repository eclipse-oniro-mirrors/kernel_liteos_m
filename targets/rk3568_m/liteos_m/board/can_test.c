/*
 * copyright (c) 2026 huawei device co., ltd.
 * licensed under the apache license, version 2.0 (the "license");
 * you may not use this file except in compliance with the license.
 * you may obtain a copy of the license at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * unless required by applicable law or agreed to in writing, software
 * distributed under the license is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * see the license for the specific language governing permissions and
 * limitations under the license.
 *
 * can_test.c - user demo for the slave-side real-time CAN port
 * (CAN1, one canDevice per port, concurrently usable).
 * compiled in via LOSCFG_MCS_CAN (board/BUILD.gn), launched from main.c.
 * all phases run in CAN_WORK_LOOPBACK (internal loopback + self-ack) -
 * no transceiver or second node needed. every received frame is verified
 * (id, flags, len, payload); a phase passes when all frames round-trip
 * and stop returns 0. "[can] all phases done" = port healthy.
 * phases (each: init -> start -> n frames -> stop):
 *   1. lb-irq-500k   loopback + interrupt rx, 500k, 20 std data frames
 *   2. lb-poll-500k  loopback + polled rx (no irq), same traffic
 *   3. lb-hyb-500k   loopback + hybrid rx (ISR + recv backstop)
 *   4. lb-bitrate    loopback + irq at 125k/250k/1M (800k unreachable
 *                    at a 100MHz bit clock - can.h)
 *   5. lb-raw-500k   raw bit timing (brp/sjw/ts1/ts2) vs the solver
 *   6. lb-frames     std/ext x data/rtr x len 0..8 boundary sweep
 *   7. lb-flt-even   acceptance filter keeps even ids only
 *   8. lb-fd-*       CAN FD (fdEnable at init): 64B frames, BRS at 2M
 *                    data phase, legal-length sweep, polled rx, ext ids
 * replace the phase bodies with your own protocol task.
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

/* ------------------------------------------------------------------ */
/* phase driver                                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *label;
    canConfig cfg;
    uint32_t nframes;
    uint32_t ext;          /* 1 = extended ids */
    uint32_t rtr;          /* 1 = remote frames */
    uint32_t dlcSweep;    /* 1 = sweep dlc 0..8 instead of fixed 8 */
    /* CAN FD frame style (requires cfg.fdEnable): fixed length, or a
     * sweep across every legal FD length 0..8/12..64. */
    uint32_t fd;           /* 1 = send FD frames */
    uint32_t brs;          /* 1 = BRS (data phase bitrate) on FD frames */
    uint32_t fdLen;       /* fixed FD payload length (default 64) */
    uint32_t fdLenSweep; /* 1 = sweep legal FD lengths instead */
    uint32_t filterTest;  /* 1 = predict per-frame acceptance with the
                            * cfg's code/mask rule; rejected frames must
                            * NEVER surface via recv() (counted as
                            * filtered, leaks are errors) */
} canPhase;

/* legal CAN FD payload lengths (dlc codes 9..15 = 12/16/20/24/32/48/64) */
static const uint8_t g_fdLens[16] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64
};

/* frame style state (kept in tiny statics to keep the phase driver
 * readable; a real protocol task would own its own context) */
static uint32_t g_ext, g_rtr, g_len, g_fdf, g_brs;

static void DemoSetFrameStyle(uint32_t ext, uint32_t rtr, uint32_t len,
                              uint32_t fdf, uint32_t brs)
{
    g_ext = ext;
    g_rtr = rtr;
    g_len = len;
    g_fdf = fdf;
    g_brs = brs;
}

static void DemoMakeFrame(canFrame *f, uint32_t seq)
{
    uint32_t i;

    f->flags = 0;
    f->id = g_ext ? (0x10000u + seq) : (0x100u + seq);
    if (g_ext != 0) {
        f->flags |= CAN_FRAME_EFF;
    }
    if (g_rtr != 0) {
        f->flags |= CAN_FRAME_RTR;
    }
    if (g_fdf != 0) {
        f->flags |= CAN_FRAME_FDF;
        if (g_brs != 0) {
            f->flags |= CAN_FRAME_BRS;
        }
    }
    f->len = (uint8_t)g_len;
    f->reserved = 0;
    for (i = 0; i < f->len; i++) {
        f->data[i] = (uint8_t)(0xa0u ^ (i + seq));
    }
}

static int DemoFrameEqual(const canFrame *a, const canFrame *b)
{
    uint32_t i;

    if ((a->id != b->id) || (a->flags != b->flags) || (a->len != b->len)) {
        return 0;
    }
    /* RTR frames carry no data: the echo's data words are undefined,
     * compare the header only */
    if ((a->flags & CAN_FRAME_RTR) != 0) {
        return 1;
    }
    for (i = 0; i < a->len; i++) {
        if (a->data[i] != b->data[i]) {
            return 0;
        }
    }
    return 1;
}

/* busy-wait ~us microseconds (~30 loop iterations per us on a ~1.5GHz
 * A55; coarse is fine - it only backs off an EAGAIN retry loop) */
static void DemoSpinUs(uint32_t us)
{
    volatile uint32_t spin;

    for (spin = 0; spin < us * 30u; spin++) {
        __asm__ volatile("nop");
    }
}

static int DemoRunPhase(const canPhase *phase)
{
    canDevice *dev = NULL;
    canStats stats = {0};
    canBusInfo busInfo = {0};
    canFrame tx, rx;
    uint32_t sent = 0, recvd = 0, errors = 0, filtered = 0;
    uint32_t seq = 0, poll, echoSeen, shouldPass, txStuck = 0;
    int ret;

    ret = CanInit(&phase->cfg, &dev);
    if (ret != 0) {
        PRINTK("[can] [%s] Init failed: %d\n", phase->label, ret);
        return -1;
    }
    ret = CanStart(dev);
    if (ret != 0) {
        PRINTK("[can] [%s] Start failed: %d\n", phase->label, ret);
        return -1;
    }
    PRINTK("[can] [%s] up\n", phase->label);

    while (sent < phase->nframes) {
        seq++;
        if (phase->dlcSweep != 0) {
            DemoSetFrameStyle(phase->ext, phase->rtr, seq % 9, 0, 0);
        } else if (phase->fdLenSweep != 0) {
            DemoSetFrameStyle(phase->ext, phase->rtr,
                              g_fdLens[seq % 16], 1, phase->brs);
        } else {
            DemoSetFrameStyle(phase->ext, phase->rtr,
                              phase->fd != 0 ? phase->fdLen : 8,
                              phase->fd, phase->brs);
        }
        DemoMakeFrame(&tx, seq);

        /* filter phases: predict acceptance with the same rule the
         * driver programs into the hw ((id & ~mask) == (code & ~mask)).
         * predicted-reject frames must never surface via recv(). */
        if (phase->filterTest != 0) {
            shouldPass = ((tx.id & ~phase->cfg.filterMask) ==
                           (phase->cfg.filterCode & ~phase->cfg.filterMask))
                          ? 1u : 0u;
        } else {
            shouldPass = 1;
        }

        ret = CanSend(dev, &tx, CAN_DEMO_SEND_TIMEOUT_US);
        if (ret != 0) {
            PRINTK("[can] [%s] tx#%u FAILED: %d\n", phase->label, seq, ret);
            errors++;
            if (++txStuck >= 30) {
                /* abort the phase instead of hanging the whole suite on
                 * a controller that stopped completing transmissions */
                (VOID)CanGetBusState(dev, &busInfo);
                (VOID)CanGetStats(dev, &stats);
                PRINTK("[can] [%s] ABORT: %u consecutive tx failures "
                       "(bus=%u rxerr=%u txerr=%u drop%u/e%u)\n",
                       phase->label, txStuck, busInfo.state, busInfo.rxErrCnt,
                       busInfo.txErrCnt, stats.rxDropped, stats.rxErrors);
                CanStop(dev, 1000);
                return -1;
            }
            continue;
        }
        txStuck = 0;
        sent++;

        if (shouldPass == 0) {
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
                           phase->label, seq, rx.id);
                } else {
                    PRINTK("[can] [%s] flt-wait rx#%u FAILED: %d\n",
                           phase->label, seq, ret);
                }
                errors++;
                break;
            }
            if (got == 0) {
                filtered++;       /* silence = the filter did its job */
            }
        } else {
            /* drain the loopback echo. the RXSTX fifo write races the
             * TX0_REQ self-clear: right after send() returns the echo may
             * not be in the fifo yet, so retry recv on EAGAIN for a bounded
             * window (~10ms worst case) instead of failing the frame. */
            echoSeen = 0;
            for (poll = 0; poll < 200; poll++) {
                ret = CanRecv(dev, &rx);
                if (ret == CAN_EAGAIN) {
                    if (echoSeen != 0) {
                        break;              /* echo verified, fifo empty */
                    }
                    DemoSpinUs(50);
                    continue;
                }
                if (ret != 0) {
                    errors++;
                    break;
                }
                echoSeen = 1;
                if (!DemoFrameEqual(&tx, &rx)) {
                    PRINTK("[can] [%s] rx#%u mismatch: id=0x%x fl=0x%x len=%u "
                           "(want id=0x%x fl=0x%x len=%u)\n",
                           phase->label, seq, rx.id, rx.flags, rx.len,
                           tx.id, tx.flags, tx.len);
                    errors++;
                } else {
                    recvd++;
                }
            }
        }

        if ((sent % CAN_DEMO_STATS_EVERY) == 0) {
            (VOID)CanGetStats(dev, &stats);
            (VOID)CanGetBusState(dev, &busInfo);
            PRINTK("[can] [%s] r%u tx a%u/c%u/to%u rx d%u/drop%u/flt%u/dup%u"
                   "/e%u irq%u bus=%u\n",
                   phase->label, sent, stats.txAccepted, stats.txCompleted,
                   stats.txTimeouts, stats.rxDelivered, stats.rxDropped, filtered,
                   stats.rxDupSuppressed, stats.rxErrors, stats.irqCount, busInfo.state);
        }
    }

    ret = CanStop(dev, 1000);
    PRINTK("[can] [%s] Stop: %d (sent %u, echoed %u, filtered %u, errors %u)\n",
           phase->label, ret, sent, recvd, filtered, errors);
    return (ret == 0 && errors == 0 && (recvd + filtered) == sent) ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* phase table                                                         */
/* ------------------------------------------------------------------ */

static const canPhase g_phases[] = {
    {
        .label = "lb-irq-500k",
        .cfg = {
            .portId = CAN_PORT_CAN1_RK3568,
            .mode = CAN_MODE_IRQ,
            .workMode = CAN_WORK_LOOPBACK,
            .bitrate = 500000,
        },
        .nframes = 20,
    },
    {
        .label = "lb-poll-500k",
        .cfg = {
            .portId = CAN_PORT_CAN1_RK3568,
            .mode = CAN_MODE_POLL,
            .workMode = CAN_WORK_LOOPBACK,
            .bitrate = 500000,
        },
        .nframes = 20,
    },
    {
        .label = "lb-hyb-500k",
        .cfg = {
            .portId = CAN_PORT_CAN1_RK3568,
            .mode = CAN_MODE_HYBRID,
            .workMode = CAN_WORK_LOOPBACK,
            .bitrate = 500000,
        },
        .nframes = 20,
    },
    {
        .label = "lb-bd-125k",
        .cfg = {
            .portId = CAN_PORT_CAN1_RK3568,
            .mode = CAN_MODE_IRQ,
            .workMode = CAN_WORK_LOOPBACK,
            .bitrate = 125000,
        },
        .nframes = 10,
    },
    {
        .label = "lb-bd-250k",
        .cfg = {
            .portId = CAN_PORT_CAN1_RK3568,
            .mode = CAN_MODE_IRQ,
            .workMode = CAN_WORK_LOOPBACK,
            .bitrate = 250000,
        },
        .nframes = 10,
    },
    {
        .label = "lb-bd-1m",
        .cfg = {
            .portId = CAN_PORT_CAN1_RK3568,
            .mode = CAN_MODE_IRQ,
            .workMode = CAN_WORK_LOOPBACK,
            .bitrate = 1000000,
        },
        .nframes = 10,
    },
    {
        /* raw timing equal to the 500k solution at 100MHz:
         * brp=4, sjw=1, ts1=43, ts2=6 (n=50 tq, SP=88%) */
        .label = "lb-raw-500k",
        .cfg = {
            .portId = CAN_PORT_CAN1_RK3568,
            .mode = CAN_MODE_IRQ,
            .workMode = CAN_WORK_LOOPBACK,
            .useRawTiming = 1,
            .timing = { .brp = 4, .sjw = 1, .ts1 = 43, .ts2 = 6 },
        },
        .nframes = 10,
    },
    {
        .label = "lb-ext-data",
        .cfg = {
            .portId = CAN_PORT_CAN1_RK3568,
            .mode = CAN_MODE_IRQ,
            .workMode = CAN_WORK_LOOPBACK,
            .bitrate = 500000,
        },
        .nframes = 10,
        .ext = 1,
    },
    {
        .label = "lb-std-rtr",
        .cfg = {
            .portId = CAN_PORT_CAN1_RK3568,
            .mode = CAN_MODE_IRQ,
            .workMode = CAN_WORK_LOOPBACK,
            .bitrate = 500000,
        },
        .nframes = 10,
        .rtr = 1,
    },
    {
        /* dlc boundary sweep 0..8 on standard data frames */
        .label = "lb-dlc-sweep",
        .cfg = {
            .portId = CAN_PORT_CAN1_RK3568,
            .mode = CAN_MODE_IRQ,
            .workMode = CAN_WORK_LOOPBACK,
            .bitrate = 500000,
        },
        .nframes = 18,
        .dlcSweep = 1,
    },
    /* --- CAN FD (fd_enable at init; loopback, zero hardware) --- */
    {
        /* FD frames at the default 2M data phase WITHOUT bit rate
         * switching: the payload goes out at the 500k nominal rate */
        .label = "lb-fd-64b",
        .cfg = {
            .portId = CAN_PORT_CAN1_RK3568,
            .mode = CAN_MODE_IRQ,
            .workMode = CAN_WORK_LOOPBACK,
            .bitrate = 500000,
            .fdEnable = 1,
        },
        .nframes = 10,
        .fd = 1,
        .fdLen = 64,
    },
    {
        /* BRS: the payload switches to the (default 2M) data phase */
        .label = "lb-fd-brs-2m",
        .cfg = {
            .portId = CAN_PORT_CAN1_RK3568,
            .mode = CAN_MODE_IRQ,
            .workMode = CAN_WORK_LOOPBACK,
            .bitrate = 500000,
            .fdEnable = 1,
            .dataBitrate = 2000000,
        },
        .nframes = 10,
        .fd = 1,
        .brs = 1,
        .fdLen = 64,
    },
    {
        /* every legal FD payload length 0..8/12..64 with BRS: one
         * frame per length (16 lengths -> 16 frames) */
        .label = "lb-fd-sweep",
        .cfg = {
            .portId = CAN_PORT_CAN1_RK3568,
            .mode = CAN_MODE_IRQ,
            .workMode = CAN_WORK_LOOPBACK,
            .bitrate = 500000,
            .fdEnable = 1,
            .dataBitrate = 2000000,
        },
        .nframes = 16,
        .fdLenSweep = 1,
        .brs = 1,
    },
    {
        /* FD data path on the POLLED rx mode (recv() reaps the 19-word
         * fifo elements directly, no irq at all) */
        .label = "lb-fd-poll-2m",
        .cfg = {
            .portId = CAN_PORT_CAN1_RK3568,
            .mode = CAN_MODE_POLL,
            .workMode = CAN_WORK_LOOPBACK,
            .bitrate = 500000,
            .fdEnable = 1,
            .dataBitrate = 2000000,
        },
        .nframes = 10,
        .fd = 1,
        .brs = 1,
        .fdLen = 64,
    },
    {
        /* FD + 29-bit extended ids */
        .label = "lb-fd-ext-2m",
        .cfg = {
            .portId = CAN_PORT_CAN1_RK3568,
            .mode = CAN_MODE_IRQ,
            .workMode = CAN_WORK_LOOPBACK,
            .bitrate = 500000,
            .fdEnable = 1,
            .dataBitrate = 2000000,
        },
        .nframes = 10,
        .ext = 1,
        .fd = 1,
        .brs = 1,
        .fdLen = 32,
    },
    {
        /* acceptance filter: keep EVEN ids only - code 0x100 with mask
         * 0x7fe requires id bit0 == 0. odd ids must vanish inside the hw
         * filter, even ids echo-verify. */
        .label = "lb-flt-even",
        .cfg = {
            .portId = CAN_PORT_CAN1_RK3568,
            .mode = CAN_MODE_IRQ,
            .workMode = CAN_WORK_LOOPBACK,
            .bitrate = 500000,
            .filterEn = 1,
            .filterCode = 0x100,
            .filterMask = 0x7fe,
        },
        .nframes = 20,
        .filterTest = 1,
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

    (VOID)arg;

    for (i = 0; i < CAN_PHASE_NUM; i++) {
        if (DemoRunPhase(&g_phases[i]) != 0) {
            failed++;
        }
        (VOID)LOS_TaskDelay(CAN_DEMO_SETTLE_TICKS);
    }

    PRINTK("[can] all phases done%s\n",
           failed == 0 ? "" : " (WITH FAILURES)");
    return NULL;
}

UINT32 CanTestTaskInit(VOID)
{
    UINT32 taskId;
    TSK_INIT_PARAM_S param = {0};

    param.pfnTaskEntry = (TSK_ENTRY_FUNC)CanDemoTask;
    param.usTaskPrio = 15;              /* above the net demo so the
                                         * finite CAN suite finishes
                                         * before the endless net stats
                                         * spam makes the log unusable */
    param.uwStackSize = 0x2000;
    param.pcName = "can_demo";
    return LOS_TaskCreate(&taskId, &param);
}
