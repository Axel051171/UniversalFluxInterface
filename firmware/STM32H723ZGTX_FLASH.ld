/*
 * UFI Flux Engine - STM32H723ZGT6 Linker Script
 * 
 * Memory Layout:
 * - FLASH:    1MB   @ 0x08000000
 * - DTCM:     128KB @ 0x20000000 (schnellster RAM, für DMA)
 * - AXI SRAM: 320KB @ 0x24000000 (großer RAM)
 * - SRAM1:    16KB  @ 0x30000000
 * - SRAM2:    16KB  @ 0x30004000
 * - SRAM4:    16KB  @ 0x38000000 (Backup)
 */

ENTRY(Reset_Handler)

/* Stack und Heap Größen */
_Min_Heap_Size = 0x4000;   /* 16KB Heap */
_Min_Stack_Size = 0x2000;  /* 8KB Stack */

MEMORY
{
    FLASH    (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
    DTCM     (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
    AXI_SRAM (rwx) : ORIGIN = 0x24000000, LENGTH = 320K
    SRAM1    (rwx) : ORIGIN = 0x30000000, LENGTH = 16K
    SRAM2    (rwx) : ORIGIN = 0x30004000, LENGTH = 16K
    SRAM4    (rwx) : ORIGIN = 0x38000000, LENGTH = 16K
}

SECTIONS
{
    /* Interrupt Vector Table */
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } >FLASH

    /* Code */
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

    /* Read-only data */
    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } >FLASH

    /* ARM exception handling */
    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } >FLASH

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

    /* Initialized data -> AXI SRAM */
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } >AXI_SRAM AT> FLASH

    /* DTCM Section - für zeitkritische Daten */
    .dtcm (NOLOAD) :
    {
        . = ALIGN(4);
        _sdtcm = .;
        *(.dtcm)
        *(.dtcm*)
        . = ALIGN(4);
        _edtcm = .;
    } >DTCM

    /* AXI SRAM Section - für große Buffer */
    .axi_sram (NOLOAD) :
    {
        . = ALIGN(4);
        _saxi = .;
        *(.axi_sram)
        *(.axi_sram*)
        . = ALIGN(4);
        _eaxi = .;
    } >AXI_SRAM

    /* Uninitialized data (BSS) */
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
    } >AXI_SRAM

    /* Heap */
    ._user_heap_stack :
    {
        . = ALIGN(8);
        PROVIDE(end = .);
        PROVIDE(_end = .);
        . = . + _Min_Heap_Size;
        . = . + _Min_Stack_Size;
        . = ALIGN(8);
    } >AXI_SRAM

    /* USB Buffer in SRAM1 (für USB OTG) */
    .usb_buffer (NOLOAD) :
    {
        . = ALIGN(4);
        *(.usb_buffer)
        . = ALIGN(4);
    } >SRAM1

    /* Backup SRAM */
    .backup (NOLOAD) :
    {
        . = ALIGN(4);
        *(.backup)
        . = ALIGN(4);
    } >SRAM4

    /* Stack am Ende von DTCM (schneller Zugriff) */
    _estack = ORIGIN(DTCM) + LENGTH(DTCM);

    /* Discard */
    /DISCARD/ :
    {
        libc.a(*)
        libm.a(*)
        libgcc.a(*)
    }

    .ARM.attributes 0 : { *(.ARM.attributes) }
}
