ENTRY(_start)
OUTPUT_FORMAT(elf64-littleaarch64)
OUTPUT_ARCH(aarch64)

SECTIONS
{
    . = 0x40080000;

    .text : ALIGN(4K)
    {
        KEEP(*(.text.boot))
        *(.text)
        *(.text.*)
        *(.text.el0)
    }

    .vectors : ALIGN(0x800)
    {
        KEEP(*(.vectors))
    }

    .rodata : ALIGN(4K)
    {
        *(.rodata)
        *(.rodata.*)
    }

    .data : ALIGN(4K)
    {
        *(.data)
        *(.data.*)
    }

    .bss : ALIGN(4K)
    {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }

    _end = .;

    /DISCARD/ :
    {
        *(.comment)
        *(.note.*)
    }
}
