// arch/x86-64/sources/arch_x64.c - Architecture setup functions
#include <arch_interface.h>
#include <ir0/print.h>
#include <ir0/panic/oops.h>
#include "tss_x64.h"
#include "arch_x64.h"
#include "gdt.h"

// I/O functions are now in arch_interface.h

// Architecture-specific initialization (called from kernel_start.c)
void arch_x64_init(void)
{
    // Setup mínimo específico de x86-64
    __asm__ volatile("cli"); // Deshabilitar interrupciones al arrancar
        
    // Initialize GDT
    gdt_install();
    
    // Initialize TSS for user mode support
    setup_tss();
}


