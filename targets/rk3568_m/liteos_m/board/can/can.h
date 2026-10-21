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
 * can.h - protocol-agnostic CAN (classic 2.0B + FD) contract for the AMP
 * slave core. any higher-layer protocol (J1939, cANopen, ...) runs on top
 * of CanSend/CanRecv; canDevice is opaque (owned by can_rk3568.c).
 * the linux master (mcs_can.ko) owns clocks/resets/pinmux and must be
 * loaded BEFORE the slave touches the port (board/DRIVER_GUIDE.md); the
 * slave owns the whole data path including its interrupt.
 * frames: classic = 11/29-bit ids, data + remote, 0..8 bytes; with
 * fdEnable = 1 also FD frames (0..64 bytes, legal lengths
 * 0..8/12/16/20/24/32/48/64; BRS = payload at the data-phase rate).
 * bit timing: bitrate (solver, ~87.5% sample point; 800k is NOT
 * reachable at the 100MHz bit clock) or raw brp/sjw/ts1/ts2. the FD
 * data phase (DBTP, 80% sample point) is configured the same two ways;
 * TDC is programmed automatically for fast data phases. rk3568 v3 FD
 * receive carries known errata - FD on an external bus is pilot-stage
 * only (rk3588 is the reliable FD IP).
 * transmit: the tx fifo is ONE frame deep; CanSend is synchronous with
 * a bounded wait (timeoutUs) and never queues behind a busy transmitter.
 * receive: CAN_MODE_IRQ = lock-free ready queue filled by the ISR;
 * CAN_MODE_POLL = CanRecv reaps the fifo directly; CAN_MODE_HYBRID = both.
 */

#ifndef _CAN_H_
#define _CAN_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct canDevice canDevice;

/* ------------------------------------------------------------------ */
/* port selection: one id per CAN controller the master handed over.  */
/* the driver holds one canDevice instance per port (CAN1 today);     */
/* more ports = one profile header + one g_ports[] entry.             */
/* ------------------------------------------------------------------ */
#define CAN_PORT_CAN1_RK3568    1    /* RK3568 CAN1 (fe580000, can1m1 pins) */

/* ------------------------------------------------------------------ */
/* rx operating mode (canConfig.mode)                                  */
/* ------------------------------------------------------------------ */
#define CAN_MODE_IRQ            0    /* interrupt driven (default) */
#define CAN_MODE_POLL           1    /* pure polling: no IRQ, recv() reaps */
#define CAN_MODE_HYBRID         2    /* IRQ driven + recv() reaps as backstop */

/* ------------------------------------------------------------------ */
/* bus operating mode (canConfig.workMode)                              */
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
/* frame                                                               */
/* ------------------------------------------------------------------ */
#define CAN_FRAME_EFF           (1u << 0)  /* 29-bit id (else 11-bit) */
#define CAN_FRAME_RTR           (1u << 1)  /* remote transmission request
                                           * (classic frames only) */
#define CAN_FRAME_FDF           (1u << 2)  /* CAN FD frame (0..64 bytes);
                                           * requires fdEnable at init */
#define CAN_FRAME_BRS           (1u << 3)  /* bit rate switching: payload
                                           * sent at dataBitrate; FDF only */

/* legal CAN FD payload lengths (dlc codes 9..15 map to 12..64 bytes) */
#define CAN_FD_MAX_LEN          64

typedef struct {
    uint32_t id;            /* 11-bit (0x0..0x7ff) or 29-bit with CAN_FRAME_EFF */
    uint8_t  flags;         /* CAN_FRAME_EFF | CAN_FRAME_RTR | CAN_FRAME_FDF
                             * | CAN_FRAME_BRS */
    uint8_t  len;           /* payload BYTES: 0..8 classic; 0..64 FD (legal
                             * FD lengths 0..8/12/16/20/24/32/48/64 - the
                             * driver maps them to the 4-bit DLC code) */
    uint8_t  reserved;
    uint8_t  data[CAN_FD_MAX_LEN];
} canFrame;

/* ------------------------------------------------------------------ */
/* bit timing (raw style)                                              */
/* ------------------------------------------------------------------ */
typedef struct {
    uint32_t brp;           /* prescaler, EVEN, 2..512 (clock tq = baudclk/brp) */
    uint32_t sjw;           /* resync jump width, 1..128 tq (1..16 data phase) */
    uint32_t ts1;           /* prop+phase1, 1..128 tq (1..32 data phase); sample
                             * point = (1+ts1)/n */
    uint32_t ts2;           /* phase2, 1..16 tq (1..16 data phase); n = 1+ts1+ts2
                             * tq per bit */
} canBittiming;

/* ------------------------------------------------------------------ */
/* init-time configuration                                            */
/* ------------------------------------------------------------------ */
#define CAN_DEFAULT_BITRATE     500000  /* canConfig.bitrate == 0 */
#define CAN_DEFAULT_DATA_BITRATE 2000000 /* canConfig.dataBitrate == 0 (FD) */

typedef struct {
    uint32_t portId;       /* CAN_PORT_*: which handed-over controller */
    uint32_t mode;          /* CAN_MODE_* (default IRQ when 0) */
    uint32_t workMode;     /* CAN_WORK_* (default NORMAL when 0) */
    /* bit timing: raw wins when useRawTiming != 0, else bitrate. */
    uint32_t useRawTiming;/* 0 = solve from bitrate (default) */
    uint32_t bitrate;       /* bits/s; CAN_DEFAULT_BITRATE when 0 */
    canBittiming timing;    /* raw values (useRawTiming = 1) */
    /* CAN FD (optional): fdEnable = 1 configures the FD data phase
     * (DBTP + TDC) and allows FDF/BRS frames; classic frames keep
     * working. an FD frame on a classic-only port is dropped and
     * counted in rxDropped. */
    uint32_t fdEnable;     /* 0 = classic CAN 2.0B only (default) */
    uint32_t dataBitrate;  /* FD data phase bits/s; CAN_DEFAULT_DATA_BITRATE
                             * when 0 */
    canBittiming dataTiming; /* raw data-phase values (useRawTiming = 1) */
    /* acceptance filter (single code/mask bank on the 29-bit id).
     * filter_en = 0 (default): accept everything.
     * filter_en = 1: accept id where (id & ~filter_mask) == (filter_code
     * & ~filter_mask); filter_mask = 0 receives exactly filter_code. */
    uint32_t filterEn;
    uint32_t filterCode;
    uint32_t filterMask;
} canConfig;

/* ------------------------------------------------------------------ */
/* runtime information                                                */
/* ------------------------------------------------------------------ */
#define CAN_BUS_STOPPED         0   /* driver not started (reset mode) */
#define CAN_BUS_ERROR_ACTIVE    1   /* rx/tx error counters < 96 */
#define CAN_BUS_ERROR_WARNING   2   /* error counter >= 96 */
#define CAN_BUS_ERROR_PASSIVE   3   /* error counter >= 128 */
#define CAN_BUS_BUS_OFF         4   /* controller off the bus */

typedef struct {
    uint32_t state;         /* CAN_BUS_* */
    uint32_t rxErrCnt;    /* hardware RX error counter 0..255 */
    uint32_t txErrCnt;    /* hardware TX error counter 0..255 */
    uint32_t busOffCount; /* bus-off events observed since init */
} canBusInfo;

typedef struct {
    uint32_t txAccepted;   /* send() calls accepted */
    uint32_t txCompleted;  /* transmissions confirmed done */
    uint32_t txErrors;     /* TX failure indicators */
    uint32_t txTimeouts;   /* send() rejected with -ETIMEDOUT */
    uint32_t rxDelivered;  /* frames copied out via recv() */
    uint32_t rxDropped;    /* malformed / filtered-out / queue-full */
    uint32_t rxErrors;     /* RX error indicators */
    uint32_t rxOverflow;   /* ready queue full at ISR time */
    uint32_t rxDupSuppressed; /* loopback self-frame duplicate swallowed */
    uint32_t irqCount;     /* INTID deliveries (interrupt modes) */
    uint32_t busOffCount; /* mirror of canBusInfo.bus_off_count */
    uint32_t reserved[3];
} canStats;

/* POSIX errno values (the bare slave has no errno.h) */
#define CAN_EAGAIN     (-11)
#define CAN_EINVAL     (-22)
#define CAN_EMSGSIZE   (-90)
#define CAN_ENODEV     (-19)
#define CAN_EBUSY      (-16)
#define CAN_EIO        (-5)
#define CAN_ETIMEDOUT  (-110)
#define CAN_ENOTSUP    (-95)

int CanInit(const canConfig *cfg, canDevice **dev);
int CanStart(canDevice *dev);
int CanStop(canDevice *dev, uint32_t timeoutUs);
int CanSend(canDevice *dev, const canFrame *frame, uint32_t timeoutUs);
int CanRecv(canDevice *dev, canFrame *frame);
int CanGetBusState(canDevice *dev, canBusInfo *info);
int CanGetStats(canDevice *dev, canStats *stats);

#ifdef __cplusplus
}
#endif

#endif /* _CAN_H_ */
