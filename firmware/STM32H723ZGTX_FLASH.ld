/*
 * UFI Flux Engine - STM32H723ZGT6 Linker Script
 * 
 * Memory Layout:
 * - FLASH:    1MB   @ 0x08000000
 * - DTCM:     128KB @ 0x20000000 (CPU/Stack; not accessible to DMA1/2)
 * - AXI SRAM: 320KB @ 0x24000000 (großer RAM)
 * - SRAM1:    16KB  @ 0x30000000
 * - SRAM2:    16KB  @ 0x30004000
 * - SRAM4:    16KB  @ 0x38000000 (D3 SRAM, not battery-backed backup SRAM)
 */

ENTRY(Reset_Handler)

/* Stack und Heap Größen */
_Min_Heap_Size = 0x2000;   /* 8 KiB bounded newlib heap; see ufi_heap.c */
_Min_Stack_Size = 0x4000;  /* enforce 16 KiB minimum CPU stack reserve */
_Min_AXI_Reserve = 0x8000; /* keep at least 32 KiB for future AXI allocations */

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
        . = ALIGN(32);
        __usb_rx_start = .;
        *(.dtcm.usb_rx)
        __usb_rx_end = .;
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

    /* Heap (AXI).  The stack lives at the end of DTCM (_estack), so its reserve is
     * checked there (ASSERT below), not padded here. */
    ._user_heap_stack :
    {
        . = ALIGN(8);
        PROVIDE(end = .);
        PROVIDE(_end = .);
        __heap_start = .;
        . = . + _Min_Heap_Size;
        . = ALIGN(8);
        __heap_end = .;
    } >AXI_SRAM
    ASSERT(__heap_end + _Min_AXI_Reserve <= ORIGIN(AXI_SRAM) + LENGTH(AXI_SRAM),
           "AXI SRAM: less than 32 KiB reserve")
    ASSERT(__usb_rx_end - __usb_rx_start == 16384, "USB RX rings: expected 16 KiB in DTCM")

    /* USB Buffer in SRAM1 (für USB OTG) */
    .usb_buffer (NOLOAD) :
    {
        . = ALIGN(4);
        *(.usb_buffer)
        . = ALIGN(4);
    } >SRAM1

    .evidence (NOLOAD) :
    {
        . = ALIGN(32);
        *(.evidence)
        . = ALIGN(32);
    } >SRAM1
    ASSERT(SIZEOF(.evidence) <= 8192, "capture evidence exceeds 8 KiB budget")

    /* D2 SRAM1: retained track survey table (op 46), CPU access only */
    .sram1 (NOLOAD) :
    {
        . = ALIGN(4);
        *(.sram1)
        *(.sram1*)
        . = ALIGN(4);
    } >SRAM1

    /* D2 SRAM2: write streaming ring (DMA1 reachable) */
    .sram2 (NOLOAD) :
    {
        . = ALIGN(32);
        *(.sram2)
        *(.sram2*)
        . = ALIGN(4);
    } >SRAM2

    /* D3 SRAM4 (not the battery-backed backup SRAM) */
    .backup (NOLOAD) :
    {
        . = ALIGN(4);
        *(.backup)
        . = ALIGN(4);
    } >SRAM4

    /* Stack am Ende von DTCM (schneller Zugriff) */
    _estack = ORIGIN(DTCM) + LENGTH(DTCM);
    /* the .dtcm data must leave the stack reserve free (audit F05) */
    ASSERT(_edtcm + _Min_Stack_Size <= _estack, "DTCM: .dtcm data leaves less than _Min_Stack_Size for the stack")

    /* Discard */
    /DISCARD/ :
    {
        libc.a(*)
        libm.a(*)
        libgcc.a(*)
    }

    .ARM.attributes 0 : { *(.ARM.attributes) }
}
