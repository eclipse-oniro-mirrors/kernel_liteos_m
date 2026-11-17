#ifndef _SOC_CONFIG_H
#define _SOC_CONFIG_H

#define IRQn_Type int

#define __CM4_REV              0x0001
#define __NVIC_PRIO_BITS       4
#define __MPU_PRESENT          1
#define __Vendor_SysTickConfig 0
#define __FPU_PRESENT          1

#define SysTick_IRQn           (-1)
#define PendSV_IRQn            (-2)
#define NonMaskableInt_IRQn    (-14)
#define MemoryManagement_IRQn  (-12)
#define BusFault_IRQn          (-11)
#define UsageFault_IRQn        (-10)
#define SVCall_IRQn            (-5)

/* STM32F407 runs on HSI 16MHz after reset; this demo keeps the default clock */
#define SYSCLK_FREQ            16000000

#endif

#include "core_cm4.h"
