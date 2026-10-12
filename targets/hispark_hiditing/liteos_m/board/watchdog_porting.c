/*
 * Copyright (c) 2026 HiSilicon (Shanghai) Technologies Co., Ltd.
 * Description: HI3322 watchdog timer (WDT V151, chip WDT)
 *
 * Base: 0x57020000, Clock: 32.768 kHz. The LOAD register's low 8 bits are
 * reserved (decrement unit = 256 clock cycles), so LOAD = seconds * 128.
 * mode=0 resets on first timeout, mode=1 resets on second timeout.
 */

#include "los_compiler.h"
#include "soc.h"
#include "watchdog.h"
#include <stdint.h>

#define WDT_CR_EN           (1u << 0)
#define WDT_CR_RST_EN       (1u << 2)
#define WDT_CR_RST_PL_SHIFT 3
#define WDT_CR_RST_PL_VAL   7u
#define WDT_CR_MODE         (1u << 7)

void hi3322_watchdog_init(uint32_t timeout_seconds, uint8_t mode)
{
    /* LOAD register: bits 0-7 reserved, bits 8-31 = actual load value.
     * Each count unit = 256 clock cycles. So:
     *   load_value = (seconds * clock_hz) / 256 = (seconds * clock_hz) >> 8
     *   register   = load_value << 8  (put in bits 8-31)
     * Equivalent to: register = seconds * clock_hz (the >>8 and <<8 cancel out).
     * Matches hs-fbb hal_watchdog_wdt_load_set_wdt_load() bitfield write. */
    uint32_t load_val = (timeout_seconds * WDT_CLOCK_HZ) >> WDT_LOAD_RESEV;

    WDT_REG_LOCK   = WDT_KEY;
    WDT_REG_EOI    = 1;
    WDT_REG_CR     = 0;

    uint32_t cr = WDT_CR_RST_EN
                | (WDT_CR_RST_PL_VAL << WDT_CR_RST_PL_SHIFT)
                | (mode ? WDT_CR_MODE : 0);

    WDT_REG_LOAD    = load_val << 8;   /* shift into bits 8-31 */
    WDT_REG_RESTART = WDT_KEY;
    WDT_REG_CR      = cr;
    WDT_REG_EOI     = 1;
    WDT_REG_CR      = cr | WDT_CR_EN;
    WDT_REG_RESTART = WDT_KEY;
}

void hi3322_watchdog_feed(void)
{
    WDT_REG_RESTART = WDT_KEY;
}

/*
 * LMS 错误上报在 LMS_LOCK 临界区(关中断)内打印影子内存 dump, 本板串口下单次
 * 上报可达 ~9s, 而 WDT(10s, WDT_TIMEOUT_SEC 见 main.c) 仅由 app_task 喂狗,
 * 上报期间会看门狗超时复位(板上表现为 LMS 用例二次上报时静默重启循环)。
 * 此强符号覆盖内核弱默认实现(kernel/extended/lms/los_lms.c), 让上报流程
 * 边打印边喂狗; 纯寄存器写, 关中断上下文可安全调用。LMS 关闭时无人引用,
 * 链接期自动裁剪。
 */
VOID OsLmsKeepaliveHook(VOID)
{
    hi3322_watchdog_feed();
}
