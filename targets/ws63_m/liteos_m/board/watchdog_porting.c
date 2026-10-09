/*
 * Copyright (c) 2024 HiSilicon (Shanghai) Technologies Co., Ltd.
 * Description: WS63 watchdog timer (WDT V151) — from 2.08.5, unchanged.
 *
 * Base: 0x40006000, Clock: 24 MHz with internal /256 prescaler.
 * mode=0 resets on first timeout, mode=1 resets on second timeout.
 */

#include "los_compiler.h"
#include <stdint.h>

#define WDT_BASE            0x40006000u
#define WDT_REG_LOCK        (*(volatile uint32_t *)(WDT_BASE + 0x00))
#define WDT_REG_LOAD        (*(volatile uint32_t *)(WDT_BASE + 0x04))
#define WDT_REG_RESTART     (*(volatile uint32_t *)(WDT_BASE + 0x08))
#define WDT_REG_EOI         (*(volatile uint32_t *)(WDT_BASE + 0x0C))
#define WDT_REG_CR          (*(volatile uint32_t *)(WDT_BASE + 0x10))

#define WDT_KEY             0x5A5A5A5Au
#define WDT_CLOCK_HZ        24000000u

#define WDT_CR_EN           (1u << 0)
#define WDT_CR_RST_EN       (1u << 2)
#define WDT_CR_RST_PL_SHIFT 3
#define WDT_CR_RST_PL_VAL   7u
#define WDT_CR_MODE         (1u << 7)

#define WDT_TIMEOUT_SEC     10

void ws63_watchdog_init(uint32_t timeout_seconds, uint8_t mode)
{
    uint32_t load_val = (timeout_seconds * WDT_CLOCK_HZ) >> 8;

    WDT_REG_LOCK   = WDT_KEY;
    WDT_REG_EOI    = 1;
    WDT_REG_CR     = 0;

    uint32_t cr = WDT_CR_RST_EN
                | (WDT_CR_RST_PL_VAL << WDT_CR_RST_PL_SHIFT)
                | (mode ? WDT_CR_MODE : 0);

    WDT_REG_LOAD    = load_val << 8;
    WDT_REG_RESTART = WDT_KEY;
    WDT_REG_CR      = cr;
    WDT_REG_EOI     = 1;
    WDT_REG_CR      = cr | WDT_CR_EN;
    WDT_REG_RESTART = WDT_KEY;
}

void ws63_watchdog_feed(void)
{
    WDT_REG_RESTART = WDT_KEY;
}
