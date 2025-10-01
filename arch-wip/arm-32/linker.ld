/* Linker script ultra simple para ARM-32 */

ENTRY(_start)

SECTIONS
{
    /* Kernel base address para ARM */
    . = 0x80000000;
    
    /* Sección de texto */
    .text : 
    {
        *(.text.boot)
        *(.text)
    }
    
    /* Sección de datos */
    .data : 
    {
        *(.data)
    }
    
    /* Sección BSS */
    .bss : 
    {
        __bss_start = .;
        *(.bss)
        *(COMMON)
        __bss_end = .;
    }
    
    /* Stack */
    .stack : 
    {
        . = . + 4096;
        stack_top = .;
    }
}
