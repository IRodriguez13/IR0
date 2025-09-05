#include <stdint.h>
#include <ir0/print.h>
#include <stddef.h>
#include <string.h>
#include "gdt.h"
#include "tss_x64.h"

// ===============================================================================
// TSS CONFIGURATION FOR USER MODE
// ===============================================================================


// Global TSS instance
tss_t kernel_tss __attribute__((aligned(16)));
// Kernel stack for Ring 0 (when returning from user mode)
static uint8_t kernel_stack[8192] __attribute__((aligned(16)));


void setup_tss()
{
    // Clear TSS structure
    memset(&kernel_tss, 0, sizeof(tss_t));

    // Set up kernel stack pointer for Ring 0
    // This is where the CPU will jump when an interrupt occurs in user mode
    kernel_tss.rsp0 = (uint64_t)(kernel_stack + sizeof(kernel_stack));

    // Set up interrupt stack table (optional, for now just use rsp0)
    kernel_tss.ist1 = kernel_tss.rsp0;
    kernel_tss.ist2 = kernel_tss.rsp0;
    kernel_tss.ist3 = kernel_tss.rsp0;
    kernel_tss.ist4 = kernel_tss.rsp0;
    kernel_tss.ist5 = kernel_tss.rsp0;
    kernel_tss.ist6 = kernel_tss.rsp0;
    kernel_tss.ist7 = kernel_tss.rsp0;

    // I/O permission bitmap (for now, no I/O access from user mode)
    kernel_tss.iopb_offset = sizeof(tss_t);

    print("setup_tss: TSS configured with RSP0 at 0x");
    print_hex64(kernel_tss.rsp0);
    print("\n");

    // Update GDT TSS descriptor with the actual TSS address
    update_gdt_tss((uint64_t)&kernel_tss);
}

