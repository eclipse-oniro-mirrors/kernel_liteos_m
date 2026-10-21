/*
 * key_port.h - 用户态平台垫片(SDK库包 专用)
 *
 * 内核版(device/soc/.../mytest/key)依赖 los_task.h/los_printf.h 的
 * LOS_TaskCreate/LOS_TaskDelay/PRINTK;用户态 SDK 在这里换成 musl 等价
 * 实现,上层 API(key_driver.h)保持与内核版完全同名同参。
 */
#ifndef KEY_PORT_H
#define KEY_PORT_H

#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define PRINTK printf   /* 与内核版打印风格对齐,输出到串口0 控制台 */

/* 毫秒级睡眠(等价内核 LOS_TaskDelay 的 1 tick=10ms 用法;被信号打断后继续睡) */
static inline void KeyDelayMs(int32_t ms)
{
    struct timespec ts;

    if (ms <= 0) {
        return;
    }
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) == -1) {
    }
}
#endif /* KEY_PORT_H */
