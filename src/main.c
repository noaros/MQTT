/* Bare-metal "hello" for Nucleo-F429ZI.
 *
 * Prints "hello" once per second on USART3 (PD8 = TX), which the Nucleo
 * routes to the ST-LINK virtual COM port. Open the port at 115200 8N1.
 *
 * Clock: the default after reset, HSI = 16 MHz. No PLL setup. */

#include <stdint.h>

#define REG(addr) (*(volatile uint32_t *)(addr))

/* RCC (reference manual RM0090 section 6.3) */
#define RCC_BASE      0x40023800
#define RCC_AHB1ENR   REG(RCC_BASE + 0x30)
#define RCC_APB1ENR   REG(RCC_BASE + 0x40)

/* GPIOD (RM0090 section 8.4) */
#define GPIOD_BASE    0x40020C00
#define GPIOD_MODER   REG(GPIOD_BASE + 0x00)
#define GPIOD_AFRH    REG(GPIOD_BASE + 0x24)

/* USART3 (RM0090 section 30.6) */
#define USART3_BASE   0x40004800
#define USART3_SR     REG(USART3_BASE + 0x00)
#define USART3_DR     REG(USART3_BASE + 0x04)
#define USART3_BRR    REG(USART3_BASE + 0x08)
#define USART3_CR1    REG(USART3_BASE + 0x0C)

/* SysTick (Cortex-M4 core peripheral) */
#define SYST_CSR      REG(0xE000E010)
#define SYST_RVR      REG(0xE000E014)
#define SYST_CVR      REG(0xE000E018)

#define HSI_HZ        16000000u
#define BAUD          115200u

volatile uint32_t ticks_ms;

void SysTick_Handler(void) {
    ticks_ms++;
}

void delay_ms(uint32_t ms) {
    uint32_t start = ticks_ms;
    while (ticks_ms - start < ms) {
    }
}

void uart_init(void) {
    RCC_AHB1ENR |= 1u << 3;   /* GPIODEN */
    RCC_APB1ENR |= 1u << 18;  /* USART3EN */

    /* PD8: alternate function mode (MODER = 0b10), AF7 = USART3_TX */
    GPIOD_MODER = (GPIOD_MODER & ~(3u << 16)) | (2u << 16);
    GPIOD_AFRH  = (GPIOD_AFRH & ~(0xFu << 0)) | (7u << 0);

    /* Oversampling by 16: BRR = fCK / baud, rounded (16 MHz / 115200 = 139) */
    USART3_BRR = (HSI_HZ + BAUD / 2) / BAUD;
    USART3_CR1 = (1u << 13) | (1u << 3);  /* UE | TE */
}

void uart_putc(char c) {
    while (!(USART3_SR & (1u << 7))) {  /* wait for TXE */
    }
    USART3_DR = (uint32_t)c;
}

void uart_puts(const char *s) {
    while (*s) {
        uart_putc(*s++);
    }
}

void systick_init(void) {
    SYST_RVR = HSI_HZ / 1000 - 1;  /* 1 ms tick */
    SYST_CVR = 0;
    SYST_CSR = 7;                  /* CLKSOURCE = core | TICKINT | ENABLE */
}

int main(void) {
    uart_init();
    systick_init();

    for (;;) {
        uart_puts("hello\r\n");
        delay_ms(1000);
    }
}
