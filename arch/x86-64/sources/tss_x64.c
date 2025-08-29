#include <stdint.h>
#include <ir0/print.h>
#include <stddef.h>
#include <string.h>

// ===============================================================================
// TSS CONFIGURATION FOR USER MODE
// ===============================================================================

// TSS structure for x86-64
typedef struct
{
    uint32_t reserved0;
    uint64_t rsp0; // Kernel stack pointer for Ring 0
    uint64_t rsp1; // Kernel stack pointer for Ring 1
    uint64_t rsp2; // Kernel stack pointer for Ring 2
    uint64_t reserved1;
    uint64_t ist1; // Interrupt stack table 1
    uint64_t ist2; // Interrupt stack table 2
    uint64_t ist3; // Interrupt stack table 3
    uint64_t ist4; // Interrupt stack table 4
    uint64_t ist5; // Interrupt stack table 5
    uint64_t ist6; // Interrupt stack table 6
    uint64_t ist7; // Interrupt stack table 7
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset; // I/O permission bitmap offset
} __attribute__((packed)) tss_t;

// Global TSS instance
static tss_t kernel_tss __attribute__((aligned(16)));

// Kernel stack for Ring 0 (when returning from user mode)
static uint8_t kernel_stack[8192] __attribute__((aligned(16)));

void setup_tss(void)
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
    extern void update_gdt_tss(uint64_t tss_addr);
    update_gdt_tss((uint64_t)&kernel_tss);
}
