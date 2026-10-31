/*
 * STM32F407VET6 USART1 driver (register level).
 *   - PA9/PA10 AF7, 115200 8N1, TX polling output (printf path)
 *   - RX polling read kept, IRQ receive intentionally dropped (not needed here)
 * USART1 kernel clock = PCLK2 = SYSCLK/1 = HSI 16MHz (reset default).
 */

#include <stdint.h>
#include "uart.h"
#include "los_interrupt.h"
#include "los_printf.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

#define REG32(addr)   (*(volatile uint32_t *)(uintptr_t)(addr))

/* RCC */
#define RCC_BASE               (0x40023800UL)
#define RCC_AHB1ENR            REG32(RCC_BASE + 0x30UL)   /* GPIOAEN: bit 0 */
#define RCC_APB2ENR            REG32(RCC_BASE + 0x44UL)   /* USART1EN: bit 4 */

/* GPIOA */
#define GPIOA_BASE_ADDR        (0x40020000UL)
#define GPIOA_MODER            REG32(GPIOA_BASE_ADDR + 0x00UL)
#define GPIOA_OSPEEDR          REG32(GPIOA_BASE_ADDR + 0x08UL)
#define GPIOA_PUPDR            REG32(GPIOA_BASE_ADDR + 0x0CUL)
#define GPIOA_AFRH             REG32(GPIOA_BASE_ADDR + 0x24UL)

/* USART1 */
#define USART1_BASE_ADDR       (0x40011000UL)
#define USART1_SR              REG32(USART1_BASE_ADDR + 0x00UL)
#define USART1_DR              REG32(USART1_BASE_ADDR + 0x04UL)
#define USART1_BRR             REG32(USART1_BASE_ADDR + 0x08UL)
#define USART1_CR1             REG32(USART1_BASE_ADDR + 0x0CUL)

#define USART_SR_RXNE          (0x1UL << 5)
#define USART_SR_TXE           (0x1UL << 7)
#define USART_CR1_UE           (0x1UL << 13)
#define USART_CR1_TE           (0x1UL << 3)
#define USART_CR1_RE           (0x1UL << 2)

#define UART_BAUDRATE          115200UL
#define UART_PCLK              16000000UL /* HSI default, APB2 prescaler = 1 */

INT32 UartPutc(INT32 c, VOID *file)
{
    (void)file;
    while ((USART1_SR & USART_SR_TXE) == 0) {
    }
    USART1_DR = (uint8_t)c;
    return c;
}

INT32 UartGetc(VOID)
{
    if ((USART1_SR & USART_SR_RXNE) == 0) {
        return 0;
    }
    return (INT32)(USART1_DR & 0xFFUL);
}

VOID UartPuts(const CHAR *s, UINT32 len, BOOL isLock)
{
    UINT32 intSave = 0;
    if (isLock == UART_WITH_LOCK) {
        intSave = LOS_IntLock();
    }
    for (UINT32 i = 0; i < len; i++) {
        UartPutc((INT32)s[i], NULL);
    }
    if (isLock == UART_WITH_LOCK) {
        LOS_IntRestore(intSave);
    }
}

VOID UartInit(VOID)
{
    uint32_t brr;

    /* enable GPIOA and USART1 clocks */
    RCC_AHB1ENR |= 0x1UL;          /* GPIOAEN */
    RCC_APB2ENR |= (0x1UL << 4);   /* USART1EN */

    /* PA9 (TX) / PA10 (RX): AF mode, high speed, pull up, AF7 = USART1 */
    GPIOA_MODER &= ~((0x3UL << 18) | (0x3UL << 20));
    GPIOA_MODER |= (0x2UL << 18) | (0x2UL << 20);
    GPIOA_OSPEEDR |= (0x3UL << 18) | (0x3UL << 20);
    GPIOA_PUPDR &= ~((0x3UL << 18) | (0x3UL << 20));
    GPIOA_PUPDR |= (0x1UL << 18) | (0x1UL << 20);
    GPIOA_AFRH &= ~((0xFUL << 4) | (0xFUL << 8));
    GPIOA_AFRH |= (0x7UL << 4) | (0x7UL << 8);

    /* 115200 8N1; BRR = fPCLK / baud with OVER8 = 0 */
    brr = (UART_PCLK + UART_BAUDRATE / 2) / UART_BAUDRATE;
    USART1_BRR = brr;

    USART1_CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}
