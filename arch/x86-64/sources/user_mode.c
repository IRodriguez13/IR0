#include <stdint.h>
#include <ir0/print.h>
#include <panic/oops.h>

// Minimal user mode support - no dynamic mapping

// User mode transition function - WITH INTERRUPTS ENABLED
void jmp_ring3(void *entry_point)
{
    // Use stack within mapped 32MB region (safe area at 16MB)
    uintptr_t stack_top = 0x1000000 - 0x1000; // 16MB - 4KB
    
    // Switch to user mode - ENABLE INTERRUPTS for keyboard
    __asm__ volatile(
        "cli\n"              // Disable interrupts temporarily during transition
        "mov $0x23, %%ax\n"  // User data segment (GDT entry 4, RPL=3)
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "pushq $0x23\n"      // SS (user stack segment)
        "pushq %0\n"         // RSP (user stack pointer)
        "pushfq\n"           // Get current RFLAGS
        "pop %%rax\n"
        "or $0x200, %%rax\n" // Set IF=1 - ENABLE interrupts in user mode
        "push %%rax\n"       // Push modified RFLAGS
        "pushq $0x1B\n"      // CS (user code segment, GDT entry 3, RPL=3)
        "pushq %1\n"         // RIP (entry point)
        "iretq\n"            // Return to Ring 3 with interrupts enabled
        :
        : "r"(stack_top), "r"((uintptr_t)entry_point)
        : "rax", "memory");

    panic("Returned from user mode unexpectedly");
}



// Syscall handler placeholder
void syscall_handler_c(void)
{
    // Syscall received
}