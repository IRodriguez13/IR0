ENTRY(_start)
OUTPUT_FORMAT(elf64-littleaarch64)
OUTPUT_ARCH(aarch64)

PHDRS
{
    text PT_LOAD FLAGS(5);   /* PF_R | PF_X */
    rodata PT_LOAD FLAGS(4); /* PF_R */
    data PT_LOAD FLAGS(6);   /* PF_R | PF_W */
}

/*
 * Linked in the TTBR1 half at KIMAGE_VADDR + KIMAGE_TEXT_OFFSET (see
 * arch/arm64/include/asm/boot.h); entry.S maps it there over whatever
 * 2 MiB aligned base the loader used. The load addresses describe the
 * QEMU virt placement for ELF consumers.
 */
KIMAGE_VADDR = 0xffff800000000000;
KIMAGE_LOAD = 0x40000000;
KIMAGE_HEAP_OFFSET = 0x800000;

SECTIONS
{
    . = KIMAGE_VADDR + 0x80000;

    .text : AT(ADDR(.text) - KIMAGE_VADDR + KIMAGE_LOAD) ALIGN(4K)
    {
        KEEP(*(.text.boot))
        *(.text)
        *(.text.*)
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
    arm64_image_size = _end - _start;
    ASSERT(_end <= KIMAGE_VADDR + KIMAGE_HEAP_OFFSET,
           "arm64: kernel image overlaps the early heap")

    /DISCARD/ :
    {
        *(.comment)
        *(.note.*)
    }
}
