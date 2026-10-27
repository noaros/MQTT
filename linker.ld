/* Linker script for STM32F429ZI: 2 MB flash, 192 KB contiguous SRAM
 * (SRAM1 112K + SRAM2 16K + SRAM3 64K). CCM RAM is not used. */

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 2048K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 192K
}

/* Initial stack pointer: top of RAM (stack grows down). */
_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
    /* Vector table must be first in flash: the core reads the initial SP
     * from 0x08000000 and the reset vector from 0x08000004. */
    .isr_vector :
    {
        KEEP(*(.isr_vector))
    } > FLASH

    .text :
    {
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    /* Initialized data: lives in RAM at runtime, its initial values are
     * stored in flash right after .text and copied by Reset_Handler. */
    .data :
    {
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH

    _sidata = LOADADDR(.data);

    /* Zero-initialized data: Reset_Handler clears it. */
    .bss :
    {
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM
}
