/*
 * Copyright (c) 2026 HiSilicon (Shanghai) Technologies Co., Ltd. All rights reserved.
 * Description: HI3322 DW APB timer board contract (drivers/timer)
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 * of conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific prior written
 * permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _ASM_REGISTER_CONFIG_H

#define _ASM_REGISTER_CONFIG_H
#define DW_APB_TIMER_BASE         TIMER0_BASE
#define DW_APB_TIMER_NEED_UNLOCK     1
#define DW_APB_TIMER_LOCK_BASE       TIMER_BASE_ADDR
#define DW_APB_TIMER_UNLOCK_VAL      0x5A5A5A5Au
#define DW_APB_TIMER_CURRENT_BROKEN  1
#define DW_APB_TIMER_EOI_FENCE       1
#define DW_APB_TIMER_REARM_ON_FIRST_RELOAD 1

#include "soc.h"

#endif /* _ASM_REGISTER_CONFIG_H */
