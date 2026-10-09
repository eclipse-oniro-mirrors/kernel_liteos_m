/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd. All rights reserved.
 *
 * MICA hybrid deployment framework - slave core (LiteOS-M) configuration.
 * Ported from the UniProton mica component
 * (src/component/mica/openamp_common.h) for the LiteOS-M RK3568 AMP port.
 *
 * This code is based on software developed by Huawei Technologies Co., Ltd
 * for the openEuler mcs project, licensed under Mulan PSL v2.
 * http://license.coscl.org.cn/MulanPSL2
 *
 * Description: protocol constants shared between the slave-side rpmsg
 * backend and its resource table. All values must match the master-side
 * (mcs_km.ko + libmica) expectations.
 */

#ifndef MICA_CFG_H__
#define MICA_CFG_H__

/*
 * OpenAMP shared memory window on RK3568:
 *   0x74000000 .. 0x77ffffff (64 MB) is reserved in the master-core DTS
 *   (mcs memory region). The firmware image itself is loaded at 0x74100000
 *   by libmica. Vrings and rpmsg buffers live at the start of the window
 *   (allocated by the master: 0x74060000 area).
 */
#define VDEV_START_ADDR         0x74000000UL
#define SHM_SIZE                0x100000UL   /* 1 MB shm window for vrings/buffers */

#define VRING_COUNT             2

/* da = -1: allocated and written back into the resource table by the master */
#define VRING_RX_ADDRESS        (-1)
#define VRING_TX_ADDRESS        (-1)
#define VRING_BUFF_ADDRESS      (-1)

#define VRING_ALIGNMENT         4
#define NUM_RPMSG_BUFF          8
#define VRING_SIZE              16

#define VIRTIO_ID_RPMSG         7
#define RPMSG_IPU_C0_FEATURES   1

/* vendor resource carrying the bound endpoint list */
#define RSC_VENDOR_EPT_TABLE    128
#define MAX_NUM_OF_EPTS         64

/* slave-side virtio device identity */
#define VDEV_ID                 0xFF
#define VRING0_ID               0 /* (master to remote) fixed to 0 for Linux compatibility */
#define VRING1_ID               1 /* (remote to master) fixed to 1 for Linux compatibility */

/* GICv3 INTID of the SGI used to kick the master core (core0) */
#define MICA_NOTIFY_IRQ_ID      7

#endif /* MICA_CFG_H__ */
