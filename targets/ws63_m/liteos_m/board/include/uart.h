/*
 * Copyright (c) 2024 HiSilicon (Shanghai) Technologies Co., Ltd.
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
 */

/*
 * Description: WS63 board-level UART abstraction header.
 * Provides the declarations the LiteOS shell (shmsg.c) and uart_porting.c
 * rely on. Modeled after device/qemu/arm_mps2_an386/liteos_m/board/include/uart.h
 * so that shmsg.c's "#include \"uart.h\"" resolves to a board-provided header,
 * consistent with the OpenHarmony liteos_m board porting convention.
 */

#ifndef _UART_H
#define _UART_H

#include "los_compiler.h"
#include "los_event.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

extern VOID UartPuts(const CHAR *s, UINT32 len, BOOL isLock);
extern INT32 UartGetc(VOID);

extern EVENT_CB_S g_shellInputEvent;

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */
#endif /* _UART_H */
