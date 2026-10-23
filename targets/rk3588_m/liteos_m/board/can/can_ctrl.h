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
 * can_ctrl.h - shared control block between the Linux master (mcs_can.ko)
 * and the LiteOS-M slave (can_rk3588.c) for the RK3588 real-time CAN
 * ports. Slave-side copy; the master copy in
 * device/board/maccura/rk3588_amp/can/can_ctrl.h is the twin.
 *
 * Layout is a contract: any change bumps CAN_CTRL_VERSION and must be
 * mirrored on both sides.
 *
 * v2 (RK3588): +fd_capable - the RK3588 CAN IP is CAN-FD capable; the
 * master publishes the capability and the data-phase clock facts stay in
 * baudclk_hz (single bit clock for both phases).
 *
 * Location: per-port blocks in the last 4KB page of the 1MB MCS shared
 * window, one 0x100 page per handed-over controller:
 *   0x740ff000..0x740ff0ff  net control block (net_ctrl.h)
 *   0x740ff100..0x740ff1ff  CAN1 control block (can1_profile.h)
 *   0x740ff200..0x740ff2ff  CAN2 control block (can2_profile.h)
 *
 * The master publishes the platform facts ONCE at probe and never touches
 * CAN registers after the bus probe; the slave owns the whole CAN data
 * path (classic + FD) and writes its own state here.
 */

#ifndef _CAN_CTRL_SLAVE_H_
#define _CAN_CTRL_SLAVE_H_

#include <stdint.h>

#define CAN_CTRL_SIZE           0x100

#define CAN_CTRL_MAGIC          0x43414E31u  /* "CAN1" (protocol id, not
                                              * a port number - the port
                                              * lives in port_index) */
#define CAN_CTRL_VERSION        2

/* master_state / slave_state */
#define CAN_CTRL_STATE_OFFLINE  0
#define CAN_CTRL_STATE_READY    1
#define CAN_CTRL_STATE_RUNNING  2
#define CAN_CTRL_STATE_FAULT    3

/* port_index */
#define CAN_PORT_CAN1           1
#define CAN_PORT_CAN2           2

struct can_ctrl {
    volatile uint32_t magic;        /* CAN_CTRL_MAGIC when master is up */
    volatile uint32_t version;      /* CAN_CTRL_VERSION (2) */
    volatile uint32_t master_state; /* written by master (mcs_can.ko) */
    volatile uint32_t slave_state;  /* written by slave (Can runtime) */

    volatile uint32_t port_index;   /* CAN_PORT_CAN1 / CAN_PORT_CAN2 */
    volatile uint32_t base_phys;    /* 0xfea60000 / 0xfea70000 */
    volatile uint32_t intid;        /* 374 / 375 = GIC_SPI 342/343 + 32
                                     * (slave-owned) */
    volatile uint32_t baudclk_hz;   /* READ-BACK CLK_CANx rate (bit timing;
                                     * serves arbitration AND data phase) */

    volatile uint32_t pclk_hz;      /* PCLK_CANx rate (informational) */
    volatile uint32_t fd_capable;   /* 1 = CAN-FD capable silicon
                                     * (RK3588 IP: always 1) */
    volatile uint32_t _reserved[4]; /* pad to 64B (cache line) */
};

#endif /* _CAN_CTRL_SLAVE_H_ */
