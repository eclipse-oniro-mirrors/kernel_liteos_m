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
 * timer.h - irq-mode SoC timer contract for the AMP slave core.
 * the underlying SoC timer channel must be one the master kernel does
 * NOT use (the oh rk3568 master owns TIMER0 as its clockevent, so the
 * slave driver binds TIMER1), and the slave owns it end to end: the
 * driver enables its CRU clock gate itself - no master-side hand-over,
 * no ko, no dts change (unlike net/can). higher layers use it for
 * cycle scheduling, timeout deadlines and periodic sampling (the slave
 * tick itself stays on the ARM generic timer and is untouched).
 * timerDevice is opaque (owned by timer_rk3568.c).
 * minimal set: interrupt-driven expiry only (no poll mode; add later
 * if a caller cannot afford the irq line).
 */

#ifndef _TIMER_H_
#define _TIMER_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct timerDevice timerDevice;

/* ------------------------------------------------------------------ */
/* port selection: one id per SoC timer channel the driver supports.   */
/* the driver holds one timerDevice per port (TIMER1 today - TIMER0 is */
/* owned by the master clockevent, see the profile header); more       */
/* channels = one profile header + one g_ports[] entry.                */
/* ------------------------------------------------------------------ */
#define TIMER_PORT_TIMER1_RK3568  1  /* RK3568 TIMER1 (fe5f0020, 24MHz) */

/* ------------------------------------------------------------------ */
/* work mode (timerConfig.mode)                                        */
/* ------------------------------------------------------------------ */
#define TIMER_MODE_ONESHOT        0  /* counts once, stops on expiry */
#define TIMER_MODE_PERIODIC       1  /* auto-reloads, fires every interval */

/* ------------------------------------------------------------------ */
/* init-time configuration                                            */
/* ------------------------------------------------------------------ */
typedef void (*timerCallback)(void *arg);

typedef struct {
    uint32_t portId;      /* TIMER_PORT_* */
    uint32_t mode;        /* TIMER_MODE_ONESHOT / TIMER_MODE_PERIODIC (required) */
    uint64_t intervalUs;  /* expiry period; 1us..~179s at 24MHz */
    timerCallback callback;  /* may be NULL: the interrupt still fires,
                              * only the user callback is skipped */
    void *callbackArg;
} timerConfig;

/* POSIX errno values (the bare slave has no errno.h) */
#define TIMER_EINVAL    (-22)
#define TIMER_ENODEV    (-19)
#define TIMER_EBUSY     (-16)
#define TIMER_EIO       (-5)

/* init (binds the port, programs CRU clock + interval) / start / stop.
 * stop is synchronous: the interrupt is released with ArchHwiDelete
 * and the channel is disabled. */
int TimerInit(const timerConfig *cfg, timerDevice **dev);
int TimerStart(timerDevice *dev);
int TimerStop(timerDevice *dev);
int TimerDeinit(timerDevice *dev);

/* change the interval at runtime (reload + keep running); works in
 * every mode, including from inside the callback. */
int TimerSetInterval(timerDevice *dev, uint64_t intervalUs);

#ifdef __cplusplus
}
#endif

#endif /* _TIMER_H_ */
