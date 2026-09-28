ENTRY(_start)
OUTPUT_FORMAT(elf64-littleaarch64)
OUTPUT_ARCH(aarch64)

PHDRS
{
    text PT_LOAD FLAGS(5);   /* PF_R | PF_X */
    rodata PT_LOAD FLAGS(4); /* PF_R */
    data PT_LOAD FLAGS(6);   /* PF_R | PF_W */
}

SECTIONS
{
    . = 0x40080000;

    .text : ALIGN(4K)
    {
        KEEP(*(.text.boot))
        *(.text)
        *(.text.*)
        *(.text.el0)
    } :text

    .vectors : ALIGN(0x800)
    {
        KEEP(*(.vectors))
    } :text

    .rodata : ALIGN(4K)
    {
        *(.rodata)
        *(.rodata.*)
    } :rodata

    .eh_frame : ALIGN(8)
    {
        *(.eh_frame)
        *(.eh_frame.*)
    } :rodata

    .data : ALIGN(4K)
    {
        *(.data)
        *(.data.*)
    } :data

    .got : ALIGN(8)
    {
        *(.got)
        *(.got.*)
        *(.got.plt)
    } :data

    .bss : ALIGN(4K)
    {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    } :data

    _end = .;

    /DISCARD/ :
    {
        *(.comment)
        *(.note.*)
    }
}
