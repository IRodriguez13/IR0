ENTRY(_start)
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)

SECTIONS
{
    . = 0x100000;

    .multiboot : ALIGN(4K)
    {
        *(.multiboot)
    }

    .text : ALIGN(4K)
    {
        *(.text)
        *(.text.*)
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

    /* Sección específica para tablas de páginas - dentro del rango mapeado */
    .paging : ALIGN(4K)
    {
        *(.paging)
    }

    .bss : ALIGN(4K)
    {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }

    /* Definir símbolo _end para el bump_allocator */
    _end = .;

    /DISCARD/ :
    {
        *(.comment)
        *(.note.*)
    }
}
