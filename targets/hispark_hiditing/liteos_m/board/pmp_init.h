/*
 * Copyright (c) 2026 HiSilicon (Shanghai) Technologies Co., Ltd.
 * Description: PMP initialization for OHOS SRAM (HI3322)
 */

#ifndef _PMP_INIT_H_
#define _PMP_INIT_H_

#include <stdint.h>

void pmp_init(void);

/* Dynload DSO pool in the unused ITCM tail; NAPOT needs power-of-two size and alignment. */
#define HI3322_DYNLOAD_POOL_BASE 0x40000u
#define HI3322_DYNLOAD_POOL_SIZE 0x10000u /* 64KB */

#endif /* _PMP_INIT_H_ */
