/* Minimal startup for STM32F429ZI: vector table + reset handler. */

#include <stdint.h>

/* Symbols defined in linker.ld */
extern uint32_t _estack;
extern uint32_t _sidata, _sdata, _edata;
extern uint32_t _sbss, _ebss;

int main(void);
void SysTick_Handler(void);

void Reset_Handler(void);
void Default_Handler(void);

/* Cortex-M4 system exceptions (first 16 entries). Device IRQs would follow,
 * but none are used yet, so the table stops at SysTick. */
__attribute__((section(".isr_vector"), used))
void (*const vector_table[16])(void) = {
    (void (*)(void))&_estack, /* 0: initial stack pointer */
    Reset_Handler,            /* 1: reset */
    Default_Handler,          /* 2: NMI */
    Default_Handler,          /* 3: HardFault */
    Default_Handler,          /* 4: MemManage */
    Default_Handler,          /* 5: BusFault */
    Default_Handler,          /* 6: UsageFault */
    0, 0, 0, 0,               /* 7-10: reserved */
    Default_Handler,          /* 11: SVCall */
    Default_Handler,          /* 12: DebugMonitor */
    0,                        /* 13: reserved */
    Default_Handler,          /* 14: PendSV */
    SysTick_Handler,          /* 15: SysTick */
};

void Reset_Handler(void)
{
    /* Copy .data initial values from flash to RAM */
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata)
        *dst++ = *src++;

    /* Zero .bss */
    for (dst = &_sbss; dst < &_ebss; dst++)
        *dst = 0;

    main();

    for (;;)
        ;
}

void Default_Handler(void)
{
    for (;;)
        ;
}
