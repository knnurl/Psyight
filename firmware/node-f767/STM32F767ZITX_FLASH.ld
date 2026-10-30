/* STM32F767ZI: 2 MB flash, 512 KB RAM (DTCM 128 KB + SRAM1 368 KB + SRAM2 16 KB, contiguous).
 * The last 256 KB of flash hold the node's configuration (board/config_store.c), so code stops
 * short of it. The D-cache stays off, so DMA buffers need no cache maintenance; but the first
 * 128 KB is the core's DTCM, which DMA reaches only through its one AHBS port, shared with the
 * Ethernet DMA (whose receive pool lands there) and below the CPU's own accesses. The LED
 * output's DMA reads a word on every bit and cannot wait, so its buffers live in LEDRAM, the
 * top of SRAM1 and SRAM2, away from DTCM (ws2812.c: section .led_dma).
 * Symbols follow ST's startup file (cmsis_device_f7 startup_stm32f767xx.s). */

ENTRY(Reset_Handler)

_estack = ORIGIN(RAM) + LENGTH(RAM);
_Min_Heap_Size = 0x2000;
_Min_Stack_Size = 0x2000;

MEMORY
{
    FLASH  (rx)  : ORIGIN = 0x08000000, LENGTH = 1792K
    RAM    (xrw) : ORIGIN = 0x20000000, LENGTH = 448K
    LEDRAM (rw)  : ORIGIN = 0x20070000, LENGTH = 64K
}

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } >FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.glue_7)
        *(.glue_7t)
        *(.eh_frame)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
        _etext = .;
    } >FLASH

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } >FLASH

    .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
    .ARM :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } >FLASH

    .preinit_array :
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } >FLASH
    .init_array :
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } >FLASH
    .fini_array :
    {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } >FLASH

    _sidata = LOADADDR(.data);

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } >RAM AT> FLASH

    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        __bss_start__ = _sbss;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        __bss_end__ = _ebss;
    } >RAM

    /* The LED output's DMA buffers, out of DTCM. Not zeroed at start: written before use. */
    .led_dma (NOLOAD) :
    {
        . = ALIGN(4);
        *(.led_dma)
        *(.led_dma*)
        . = ALIGN(4);
    } >LEDRAM

    ._user_heap_stack :
    {
        . = ALIGN(8);
        PROVIDE(end = .);
        PROVIDE(_end = .);
        . = . + _Min_Heap_Size;
        . = . + _Min_Stack_Size;
        . = ALIGN(8);
    } >RAM

    /DISCARD/ :
    {
        libc.a(*)
        libm.a(*)
        libgcc.a(*)
    }

    .ARM.attributes 0 : { *(.ARM.attributes) }
}
