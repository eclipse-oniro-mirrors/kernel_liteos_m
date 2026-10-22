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
 * can.h - generic real-time CAN (classic 2.0B + CAN-FD) contract for the
 * AMP slave core, RK3588 port.
 *
 * Protocol-agnostic frame-level driver in the same style as net.h: any
 * higher-layer CAN protocol (J1939, CANopen, proprietary) runs on top of
 * CanSend/CanRecv. CanDevice is opaque and owned by the board driver
 * (can_rk3588.c).
 *
 * Frame contract v2 (RK3588): the CAN IP is CAN-FD capable and this
 * contract covers BOTH frame types:
 *   - classic CAN 2.0B: standard (11-bit) and extended (29-bit) ids,
 *     data and remote frames, 0..8 payload bytes, dlc 0..8.
 *   - CAN-FD: frames flagged CAN_FRAME_FDF, payload 0..64 bytes via the
 *     on-wire DLC 0..15 (9..15 encode 12/16/20/24/32/48/64 bytes -
 *     CanFdDlc2Len()/CanFdLen2Dlc() convert). CAN_FRAME_BRS marks a
 *     bit-rate-switching frame (switches to the data-phase bitrate after
 *     the arbitration phase; requires FDF). Remote (RTR) frames are
 *     classic-only: RTR+FDF is rejected.
 * CanFrame.dlc ALWAYS carries the ON-WIRE DLC; the payload length is
 * CanFdDlc2Len(dlc) for FDF frames and min(dlc,8) otherwise.
 *
 * Division of labor: the Linux master (mcs_can.ko) owns clocks (baudclk
 * retargeted to 100MHz and held on), CRU resets, the pinmux and publishes
 * the platform facts into the per-port shared control block (can_ctrl.h;
 * one 0x100 page per handed-over controller); this driver owns the whole
 * data path: operating mode, bit timing (arbitration + data phase),
 * acceptance filters, the rx fifo, the single-frame tx fifo handshake and
 * the CAN interrupts (registered via ArchHwiCreate - per-core GICD
 * ownership, the master never touches them).
 *
 * Bit timing, arbitration phase (CanConfig.timing / bitrate): two styles.
 *   - bitrate (default): the driver solves prescaler/tseg for a ~87.5%
 *     sample point against the READ-BACK baudclk published by the master.
 *     800kbps is NOT reachable at a 100MHz bit clock (100M/800k = 125,
 *     the prescaler steps by 2 - no even solution).
 *   - raw brp/sjw/ts1/ts2 (use_raw_timing = 1): expert escape hatch,
 *     values are written straight into NBTP. Ranges: brp even 2..512,
 *     sjw/ts1/ts2 1..128.
 * Bit timing, data phase (fd_mode = 1 only): same two styles through
 * data_bitrate / use_raw_data_timing + data_timing, solved for a ~75%
 * sample point. Data-phase segment ranges are tighter: ts1 1..32,
 * ts2 1..16, sjw 1..16 (DBTP field widths). 4M and 8M are unreachable
 * at 100MHz with an even prescaler; 1M/2M/2.5M/5M/6.25M work.
 *
 * TDC (transmitter delay compensation) is deliberately DISABLED (TDCR
 * kept at 0) for every data bitrate: BRS reception is verified working
 * with TDC off on this IP family, and the default 2M data rate is far
 * below the bitrate range where TDC matters.
 *
 * Transmit model: the RK3588 CAN tx fifo is ONE frame deep; CanSend is
 * synchronous - it queues the frame, completes the hardware handshake and
 * waits (bounded by timeout_us) for the transmission to finish before
 * returning. Send never queues behind a busy transmitter.
 *
 * Receive model: CAN_MODE_IRQ delivers frames through a lock-free ready
 * queue filled by the ISR; CAN_MODE_POLL never enables the interrupt and
 * CanRecv reaps the hardware fifo directly; CAN_MODE_HYBRID does both
 * (the ISR reaps ahead, Recv reaps again as a backstop).
 */

#ifndef _CAN_H_
#define _CAN_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CanDevice CanDevice;

/* ------------------------------------------------------------------ */
/* Port selection: one id per CAN controller the master handed over.  */
/* The driver holds one CanDevice instance per port - both CAN1 and   */
/* CAN2 can run CONCURRENTLY (independent fifos, irqs, ctrl blocks). */
/* ------------------------------------------------------------------ */
#define CAN_PORT_CAN1_RK3588    1    /* RK3588 CAN1 (fea60000, can1m1 pins) */
#define CAN_PORT_CAN2_RK3588    2    /* RK3588 CAN2 (fea70000, can2m1 pins) */

/* ------------------------------------------------------------------ */
/* Rx operating mode (CanConfig.mode)                                  */
/* ------------------------------------------------------------------ */
#define CAN_MODE_IRQ            0    /* interrupt driven (default) */
#define CAN_MODE_POLL           1    /* pure polling: no IRQ, Recv() reaps */
#define CAN_MODE_HYBRID         2    /* IRQ driven + Recv() reaps as backstop */

/* ------------------------------------------------------------------ */
/* Bus operating mode (CanConfig.work_mode)                            */
/* ------------------------------------------------------------------ */
#define CAN_WORK_NORMAL         0    /* normal bus participation (default) */
#define CAN_WORK_LOOPBACK       1    /* internal loopback, self-ack, nothing
                                        driven on the pins: the zero-
                                        hardware bring-up / unit-test mode */
#define CAN_WORK_SILENT         2    /* listen-only: receive only, no ack,
                                        no transmit (bus monitoring) */
#define CAN_WORK_SILENT_LB      3    /* loopback + listen-only: internal
                                        loopback, nothing on the pins */

/* ------------------------------------------------------------------ */
/* Frame                                                               */
/* ------------------------------------------------------------------ */
#define CAN_FRAME_EFF           (1u << 0)  /* 29-bit id (else 11-bit) */
#define CAN_FRAME_RTR           (1u << 1)  /* remote transmission request
                                            * (classic frames only) */
#define CAN_FRAME_FDF           (1u << 2)  /* CAN-FD frame (v2) */
#define CAN_FRAME_BRS           (1u << 3)  /* bit rate switching (needs FDF) */

typedef struct {
    uint32_t id;            /* 11-bit (0x0..0x7ff) or 29-bit with CAN_FRAME_EFF */
    uint8_t  flags;         /* CAN_FRAME_EFF|RTR|FDF|BRS */
    uint8_t  dlc;           /* on-wire DLC: classic 0..8, FD 0..15 (payload
                             * length = CanFdDlc2Len(dlc) for FDF frames) */
    uint8_t  reserved;
    uint8_t  data[64];      /* payload, FDF frames up to 64 bytes */
} CanFrame;

/* CAN-FD DLC <-> payload length (9..15 encode 12/16/20/24/32/48/64). */
static inline uint32_t CanFdDlc2Len(uint32_t dlc)
{
    static const uint8_t len[16] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64
    };
    return len[dlc & 0xfu];
}

static inline uint32_t CanFdLen2Dlc(uint32_t len)
{
    if (len <= 8u) {
        return len;
    }
    if (len <= 12u) {
        return 9;
    }
    if (len <= 16u) {
        return 10;
    }
    if (len <= 20u) {
        return 11;
    }
    if (len <= 24u) {
        return 12;
    }
    if (len <= 32u) {
        return 13;
    }
    if (len <= 48u) {
        return 14;
    }
    return 15;
}

/* ------------------------------------------------------------------ */
/* Bit timing (raw style)                                              */
/*                                                                     */
/* One struct serves both phases; the DRIVER validates against the     */
/* per-phase hardware limits when writing NBTP / DBTP:                 */
/*   arbitration (NBTP): brp even 2..512, sjw/ts1/ts2 1..128           */
/*   data (DBTP):        brp even 2..512, sjw 1..16, ts1 1..32,        */
/*                      ts2 1..16                                      */
/* ------------------------------------------------------------------ */
typedef struct {
    uint32_t brp;           /* prescaler, EVEN, 2..512 (clock tq = baudclk/brp) */
    uint32_t sjw;           /* resync jump width, 1..128 tq (data: 1..16) */
    uint32_t ts1;           /* prop+phase1, 1..128 tq (data: 1..32); sample
                             * point = (1+ts1)/N */
    uint32_t ts2;           /* phase2, 1..128 tq (data: 1..16); N = 1+ts1+ts2
                             * tq per bit */
} CanBittiming;

/* ------------------------------------------------------------------ */
/* Init-time configuration                                            */
/* ------------------------------------------------------------------ */
typedef struct {
    uint32_t port_id;       /* CAN_PORT_*: which handed-over controller */
    uint32_t mode;          /* CAN_MODE_* (default IRQ when 0) */
    uint32_t work_mode;     /* CAN_WORK_* (default NORMAL when 0) */
    /* Arbitration-phase bit timing: raw wins when use_raw_timing != 0. */
    uint32_t use_raw_timing;/* 0 = solve from bitrate (default) */
    uint32_t bitrate;       /* bits/s; default 500000 when both styles are 0 */
    CanBittiming timing;    /* raw values (use_raw_timing = 1) */
    /* Acceptance filter (single code/mask bank on the 29-bit id).
     * filter_en = 0 (default): accept everything.
     * filter_en = 1: accept id where (id & ~filter_mask) == (filter_code
     * & ~filter_mask); filter_mask = 0 receives exactly filter_code. */
    uint32_t filter_en;
    uint32_t filter_code;
    uint32_t filter_mask;
    /* --- CAN-FD (v2): fd_mode = 1 arms the FD data phase ----------- */
    uint32_t fd_mode;       /* 0 = classic CAN 2.0B only (default),
                             * 1 = CAN-FD capable (classic frames still
                             * legal; FDF frames accepted on TX/RX) */
    uint32_t data_bitrate;  /* data-phase bits/s; default 2000000 when 0
                             * and fd_mode = 1 */
    uint32_t use_raw_data_timing; /* 0 = solve from data_bitrate (default) */
    CanBittiming data_timing;     /* raw data-phase values when
                                   * use_raw_data_timing = 1 */
} CanConfig;

/* ------------------------------------------------------------------ */
/* Runtime information                                                */
/* ------------------------------------------------------------------ */
#define CAN_BUS_STOPPED         0   /* driver not started (reset mode) */
#define CAN_BUS_ERROR_ACTIVE    1   /* rx/tx error counters < 96 */
#define CAN_BUS_ERROR_WARNING   2   /* error counter >= 96 */
#define CAN_BUS_ERROR_PASSIVE   3   /* error counter >= 128 */
#define CAN_BUS_BUS_OFF         4   /* controller off the bus */

typedef struct {
    uint32_t state;         /* CAN_BUS_* */
    uint32_t rx_err_cnt;    /* hardware RX error counter 0..255 */
    uint32_t tx_err_cnt;    /* hardware TX error counter 0..255 */
    uint32_t bus_off_count; /* bus-off events observed since Init */
} CanBusInfo;

typedef struct {
    uint32_t tx_accepted;   /* Send() calls accepted */
    uint32_t tx_completed;  /* transmissions confirmed done */
    uint32_t tx_errors;     /* TX failure indicators */
    uint32_t tx_timeouts;   /* Send() rejected with -ETIMEDOUT */
    uint32_t rx_delivered;  /* frames copied out via Recv() */
    uint32_t rx_dropped;    /* malformed / filtered-out / queue-full */
    uint32_t rx_errors;     /* RX error indicators */
    uint32_t rx_overflow;   /* ready queue full at ISR time */
    uint32_t rx_dup_suppressed; /* loopback self-frame duplicate swallowed */
    uint32_t irq_count;     /* INTID deliveries (interrupt modes) */
    uint32_t bus_off_count; /* mirror of CanBusInfo.bus_off_count */
    uint32_t reserved[3];
} CanStats;

/* POSIX errno values (the bare slave has no errno.h) */
#define CAN_EAGAIN     (-11)
#define CAN_EINVAL     (-22)
#define CAN_EMSGSIZE   (-90)
#define CAN_ENODEV     (-19)
#define CAN_EBUSY      (-16)
#define CAN_EIO        (-5)
#define CAN_ETIMEDOUT  (-110)
#define CAN_ENOTSUP    (-95)

int CanInit(const CanConfig *cfg, CanDevice **dev);
int CanStart(CanDevice *dev);
int CanStop(CanDevice *dev, uint32_t timeout_us);
int CanSend(CanDevice *dev, const CanFrame *frame, uint32_t timeout_us);
int CanRecv(CanDevice *dev, CanFrame *frame);
int CanGetBusState(CanDevice *dev, CanBusInfo *info);
int CanGetStats(CanDevice *dev, CanStats *stats);

#ifdef __cplusplus
}
#endif

#endif /* _CAN_H_ */
