#include <stdint.h>
#include <ir0/print.h>
#include <stddef.h>
#include <string.h>
#include "gdt.h"
#include "tss_x64.h"


// Global TSS instance
tss_t kernel_tss __attribute__((aligned(4096)));

// Kernel stack for Ring 0 - 32KB for safety
static uint8_t kernel_stack[32768] __attribute__((aligned(4096)));

void setup_tss()
{
    // Clear TSS structure
    memset(&kernel_tss, 0, sizeof(tss_t));

    // Set up kernel stack pointer for Ring 0
    kernel_tss.rsp0 = (uint64_t)(kernel_stack + sizeof(kernel_stack) - 16);
    
    // IST not used - set to 0
    kernel_tss.ist1 = 0;
    kernel_tss.ist2 = 0;
    kernel_tss.ist3 = 0;
    kernel_tss.ist4 = 0;
    kernel_tss.ist5 = 0;
    kernel_tss.ist6 = 0;
    kernel_tss.ist7 = 0;

    // I/O permission bitmap
    kernel_tss.iopb_offset = sizeof(tss_t);

    // Update GDT TSS descriptor
    update_gdt_tss((uint64_t)&kernel_tss);
}

