#include <stdint.h>
#include "../../includes/ir0/print.h"

// TSS structure for x86-64
typedef struct {
    uint32_t reserved1;
    uint64_t rsp0;      // Stack pointer for privilege level 0
    uint64_t rsp1;      // Stack pointer for privilege level 1
    uint64_t rsp2;      // Stack pointer for privilege level 2
    uint64_t reserved2;
    uint64_t ist1;      // Interrupt stack table entry 1
    uint64_t ist2;      // Interrupt stack table entry 2
    uint64_t ist3;      // Interrupt stack table entry 3
    uint64_t ist4;      // Interrupt stack table entry 4
    uint64_t ist5;      // Interrupt stack table entry 5
    uint64_t ist6;      // Interrupt stack table entry 6
    uint64_t ist7;      // Interrupt stack table entry 7
    uint64_t reserved3;
    uint16_t reserved4;
    uint16_t iopb_offset;
} __attribute__((packed)) tss_entry_t;

// TSS instance
static tss_entry_t tss;

// Interrupt stack (16KB stack for interrupts)
#define INTERRUPT_STACK_SIZE 16384
static uint8_t interrupt_stack[INTERRUPT_STACK_SIZE] __attribute__((aligned(16)));

void tss_init_x64(void)
{
    print_colored("[TSS] Initializing Task State Segment for x86-64...\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    
    // Clear TSS structure
    for (int i = 0; i < sizeof(tss_entry_t); i++) {
        ((uint8_t*)&tss)[i] = 0;
    }
    
    // Set up interrupt stack pointers
    uint64_t stack_top = (uint64_t)interrupt_stack + INTERRUPT_STACK_SIZE;
    
    // Set RSP0 (kernel stack for privilege level 0)
    tss.rsp0 = stack_top;
    
    // Set IST entries (Interrupt Stack Table)
    tss.ist1 = stack_top - 4096;  // 4KB from top
    tss.ist2 = stack_top - 8192;  // 8KB from top
    tss.ist3 = stack_top - 12288; // 12KB from top
    tss.ist4 = stack_top - 16384; // 16KB from top
    tss.ist5 = stack_top - 20480; // 20KB from top (if needed)
    tss.ist6 = stack_top - 24576; // 24KB from top (if needed)
    tss.ist7 = stack_top - 28672; // 28KB from top (if needed)
    
    // IOPB offset (no I/O permission bitmap)
    tss.iopb_offset = sizeof(tss_entry_t);
    
    print_success("[TSS] TSS initialized successfully\n");
    print("[TSS] Interrupt stack top: 0x");
    print_hex64(stack_top);
    print("\n");
}

// Load TSS into GDT and set up TR register
void tss_load_x64(void)
{
    // This function should be called after GDT is set up
    // For now, we'll just print that TSS is ready
    print("[TSS] TSS ready to be loaded into GDT\n");
    print("[TSS] Use 'ltr' instruction to load TSS selector\n");
}

// Get TSS address (for GDT setup)
uint64_t tss_get_address(void)
{
    return (uint64_t)&tss;
}

// Get TSS size
uint32_t tss_get_size(void)
{
    return sizeof(tss_entry_t);
}
