OUTPUT_FORMAT("elf32-i386")
ENTRY(start)

SECTIONS
{
    . = 0x7E00;

    .start : { *(.text.start) }
    .text  : { *(.text) }
    .data  : { *(.data) *(.rodata) }

    .bss ALIGN(4) :
    {
        __bss_start = .;
        *(.bss) *(COMMON)
        __bss_end = .;
    }
}
