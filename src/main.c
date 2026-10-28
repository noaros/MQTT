/* Bare-metal temperature readout for Nucleo-F429ZI.
 *
 * Reads the internal temperature sensor once per second and prints it on
 * USART3 (PD8 = TX), which the Nucleo routes to the ST-LINK virtual COM
 * port. Open the port at 115200 8N1.
 *
 * Clock: the default after reset, HSI = 16 MHz. No PLL setup. */

#include <stdint.h>

#define REG(addr) (*(volatile uint32_t *)(addr))

/* RCC (reference manual RM0090 section 6.3) */
#define RCC_BASE      0x40023800
#define RCC_AHB1ENR   REG(RCC_BASE + 0x30)
#define RCC_APB1ENR   REG(RCC_BASE + 0x40)
#define RCC_APB2ENR   REG(RCC_BASE + 0x44)

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

/* ADC1 (RM0090 section 13.13) */
#define ADC1_BASE     0x40012000
#define ADC1_SR       REG(ADC1_BASE + 0x00)
#define ADC1_CR2      REG(ADC1_BASE + 0x08)
#define ADC1_SMPR1    REG(ADC1_BASE + 0x0C)
#define ADC1_SQR1     REG(ADC1_BASE + 0x2C)
#define ADC1_SQR3     REG(ADC1_BASE + 0x34)
#define ADC1_DR       REG(ADC1_BASE + 0x4C)
#define ADC_CCR       REG(0x40012300 + 0x04)  /* common to all ADCs */

/* Temperature sensor calibration values, measured by ST in production at
 * 30 C and 110 C with VDDA = 3.3 V (datasheet section 6.3.22) */
#define TS_CAL1       (*(volatile uint16_t *)0x1FFF7A2C)
#define TS_CAL2       (*(volatile uint16_t *)0x1FFF7A2E)

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

/* Print an unsigned number in decimal */
void uart_putu(uint32_t n) {
    char buf[10];
    int i = 0;
    do {
        buf[i++] = (char)('0' + n % 10);
        n /= 10;
    } while (n);
    while (i) {
        uart_putc(buf[--i]);
    }
}

void adc_init(void) {
    RCC_APB2ENR |= 1u << 8;  /* ADC1EN */

    /* The temperature sensor is ADC1 channel 18, shared with VBAT. Enable
     * the sensor (TSVREFE) and make sure VBAT is off (VBATE): if both are
     * set, the ADC measures VBAT. ADC clock = 16 MHz / 2 (default) = 8 MHz. */
    ADC_CCR = (ADC_CCR & ~(1u << 22)) | (1u << 23);

    /* Sensor needs >= 10 us sampling: 480 cycles at 8 MHz = 60 us.
     * Channel 18 sample time is SMPR1 bits 26:24. */
    ADC1_SMPR1 = (ADC1_SMPR1 & ~(7u << 24)) | (7u << 24);

    ADC1_SQR1 = 0;   /* sequence length 1 */
    ADC1_SQR3 = 18;  /* first conversion: channel 18 */
    ADC1_CR2 = 1u << 0;  /* ADON */

    delay_ms(1);  /* ADC and sensor startup (a few us) */
}

/* One conversion, returns the 12-bit result */
uint32_t adc_read(void) {
    ADC1_CR2 |= 1u << 30;  /* SWSTART */
    while (!(ADC1_SR & (1u << 1))) {  /* wait for EOC */
    }
    return ADC1_DR;  /* reading DR clears EOC */
}

/* Convert a raw reading to tenths of a degree C by linear interpolation
 * between the two calibration points */
int32_t temp_tenths(uint32_t raw) {
    int32_t cal1 = TS_CAL1;
    int32_t cal2 = TS_CAL2;
    return 300 + (800 * ((int32_t)raw - cal1)) / (cal2 - cal1);
}

void systick_init(void) {
    SYST_RVR = HSI_HZ / 1000 - 1;  /* 1 ms tick */
    SYST_CVR = 0;
    SYST_CSR = 7;                  /* CLKSOURCE = core | TICKINT | ENABLE */
}

int main(void) {
    uart_init();
    systick_init();
    adc_init();

    for (;;) {
        int32_t t = temp_tenths(adc_read());
        uart_puts("temp ");
        if (t < 0) {
            uart_putc('-');
            t = -t;
        }
        uart_putu((uint32_t)t / 10);
        uart_putc('.');
        uart_putu((uint32_t)t % 10);
        uart_puts(" C\r\n");
        delay_ms(1000);
    }
}
