/**
 * @file  STM32L4S5VITx_FLASH.ld
 * @brief Linker script for STM32L4S5VITx (B-L4S5I-IOT01A)
 *
 * Memory map (STM32L4S5 — L4+ family, 640 KB total SRAM):
 *   FLASH : 2048 KB  @ 0x0800_0000
 *   SRAM1 : 192  KB  @ 0x2000_0000   (general purpose)
 *   SRAM2 :  64  KB  @ 0x1000_0000   (ECC, retained in Standby)
 *   SRAM3 : 384  KB  @ 0x2004_0000   (general purpose)
 *
 * Memory placement strategy:
 *   .text / .rodata          → FLASH
 *   .data / .bss / stack     → SRAM1  (main working memory)
 *   .sram2_bss               → SRAM2  (Anbo kernel object pools — ECC protected)
 *   SRAM3 available for future use (heap, large buffers, etc.)
 *
 * Stack: top of SRAM1 (grows downward)
 */

/* Entry point */
ENTRY(Reset_Handler)

/* Highest address of the user mode stack */
_estack = ORIGIN(RAM) + LENGTH(RAM);

/* Minimum heap and stack sizes — linker will error if exceeded */
_Min_Heap_Size  = 0x0;     /* no heap — project uses only static memory */
_Min_Stack_Size = 0x8000;  /* 32 KB — SRAM1 has 179 KB free, no reason to skimp */

/* ================================================================== */
/*  Memory regions                                                     */
/* ================================================================== */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 2048K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 192K
    SRAM2 (rwx) : ORIGIN = 0x10000000, LENGTH = 64K
    SRAM3 (rwx) : ORIGIN = 0x20040000, LENGTH = 384K
}

/* ================================================================== */
/*  Sections                                                           */
/* ================================================================== */
SECTIONS
{
    /* ---- Interrupt vector table (must be at FLASH start) ---- */
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } >FLASH

    /* ---- Code ---- */
    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.glue_7)         /* ARM-to-Thumb glue */
        *(.glue_7t)        /* Thumb-to-ARM glue */
        *(.eh_frame)

        KEEP(*(.init))
        KEEP(*(.fini))

        . = ALIGN(4);
        _etext = .;
    } >FLASH

    /* ---- Read-only data ---- */
    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } >FLASH

    /* ---- ARM exception unwinding (used by C++ and -funwind-tables) ---- */
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

    /* ---- Pre-init / init / fini arrays ---- */
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

    /* ---- Initialised data (copy from FLASH to RAM at startup) ---- */
    _sidata = LOADADDR(.data);

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        *(.RamFunc)
        *(.RamFunc*)
        . = ALIGN(4);
        _edata = .;
    } >RAM AT> FLASH

    /* ---- Zero-initialised data (SRAM1) ---- */
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

    /* ---- Heap + Stack guard (SRAM1) ---- */
    ._user_heap_stack :
    {
        . = ALIGN(8);
        PROVIDE(end = .);
        PROVIDE(_end = .);
        . = . + _Min_Heap_Size;
        . = . + _Min_Stack_Size;
        . = ALIGN(8);
    } >RAM

    /* ================================================================ */
    /*  SRAM2 — Anbo kernel object pools (ECC, Standby retention)       */
    /*                                                                  */
    /*  Place ring buffer storage, event bus buckets, WDT slot pool,    */
    /*  and other critical static objects here via:                     */
    /*      __attribute__((section(".sram2_bss")))                       */
    /*                                                                  */
    /*  Zero-initialised by startup code (Reset_Handler).               */
    /* ================================================================ */
    .sram2_bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss_sram2 = .;
        *(.sram2_bss)
        *(.sram2_bss*)
        *(.sram2)
        *(.sram2*)
        . = ALIGN(4);
        _ebss_sram2 = .;
    } >SRAM2

    /* ================================================================ */
    /*  SRAM3 — Available for future use (large buffers, heap, etc.)    */
    /* ================================================================ */
    .sram3_bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss_sram3 = .;
        *(.sram3_bss)
        *(.sram3_bss*)
        . = ALIGN(4);
        _ebss_sram3 = .;
    } >SRAM3

    /* Discard debug sections from standard libraries */
    /DISCARD/ :
    {
        libc.a(*)
        libm.a(*)
        libgcc.a(*)
    }

    .ARM.attributes 0 : { *(.ARM.attributes) }
}
