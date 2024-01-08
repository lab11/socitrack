ENTRY(Reset_Handler)

MCU_MRAM_SIZE = 0x001E8000;        /* 1952kB, 1,998,848 bytes */
MCU_TCM_SIZE = 0x00060000;         /* 384kB, 393,216 bytes */
SHARED_SRAM_SIZE = 0x00100000;     /* 1024kB, 1,048,576 bytes */
EXTENDED_SRAM_SIZE = 0x00060000;   /* 384kB, 393,216 bytes */
SHARED_SRAM2_SIZE = 0x00100000;    /* 1024kB, 1,048,576 bytes */
UID_SIZE = 8;                      /* 8 bytes */

MEMORY
{
    MCU_MRAM       (rx)  : ORIGIN = 0x00018000, LENGTH = MCU_MRAM_SIZE - UID_SIZE
    UID            (r)   : ORIGIN = 0x00018000 + MCU_MRAM_SIZE - UID_SIZE, LENGTH = UID_SIZE
    MCU_TCM        (rwx) : ORIGIN = 0x10000000, LENGTH = MCU_TCM_SIZE
    SHARED_SRAM    (rwx) : ORIGIN = 0x10060000, LENGTH = SHARED_SRAM_SIZE
    EXTENDED_SRAM  (rwx) : ORIGIN = 0x10160000, LENGTH = EXTENDED_SRAM_SIZE
    SHARED_SRAM2   (rwx) : ORIGIN = 0x101C0000, LENGTH = SHARED_SRAM2_SIZE 
}

SECTIONS
{
	.isr_vector :
	{
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        KEEP(*(.patch))
        . = ALIGN(4);
	} > MCU_MRAM

    .text :
    {
        . = ALIGN(4);
        __dw_drivers_start = .;
        KEEP(*(.dw_drivers))
        __dw_drivers_end = .;
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } > MCU_MRAM

    .ARM.exidx :
    {
        __exidx_start = .;
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __exidx_end = .;
    } > MCU_MRAM

    . = ALIGN(8);
    _etext = .;

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > MCU_TCM AT>MCU_MRAM

    /* used by startup to initialize data */
    _init_data = LOADADDR(.data);

    .bss (NOLOAD):
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > MCU_TCM

    .heap (NOLOAD):
    {
        . = ALIGN(8);
        _sheap = .;
        __heap_start__ = _sheap;
        end = _sheap;
        _end = _sheap;
        __end = _sheap;
        KEEP(*(.heap))
        . = ALIGN(8);
        _eheap = .;
        __heap_end__ = _eheap;
        __HeapLimit = _eheap;
    } > MCU_TCM

    .stack (NOLOAD):
    {
        . = ALIGN(8);
        _sstack = .;
        _stack_size = ORIGIN(MCU_TCM) + LENGTH(MCU_TCM) - _sstack;
        . = . + _stack_size;
        . = ALIGN(8);
        _estack = .;
    } > MCU_TCM

    .ARM.attributes 0 : { *(.ARM.attributes) }
}

_uid_base_address = ORIGIN(UID);
