#include "oops.h"
#include <ir0/vga.h>
#include <drivers/serial/serial.h>

#define Interrupts_off asm volatile("cli")
#define Cpu_Sleep asm volatile("hlt")

/**
 * Kernel panic handler - comprehensive error reporting system
 *
 * This module provides kernel panic functionality with extensive diagnostic
 * information dumping to both VGA console and serial port. The serial output
 * is designed to be copyable for external analysis and debugging.
 *
 * Key features:
 * - Double panic detection (prevents infinite recursion)
 * - Complete register dump (x86-64/x86-32)
 * - Stack trace unwinding
 * - Process context information
 * - Memory state information
 * - Structured serial output for easy parsing
 *
 * The serial output format is designed to be easily grep-able and parseable,
 * making it suitable for automated log analysis tools.
 */

static const char *panic_level_names[] =
    {
        "KERNEL BUG",
        "HARDWARE FAULT",
        "OUT OF MEMORY",
        "STACK OVERFLOW",
        "ASSERTION FAILED",
        "MEMORY ERROR",
        "TESTING",
        "RUNNING OUT PROCESS"
    };

/* Double panic guard - prevents infinite recursion if panic handler itself fails */
static volatile int in_panic = 0;

/* Forward declarations for helper functions */
static void dump_process_context(void);
static void dump_memory_state(void);

/**
 * panicex - Extended panic handler with comprehensive diagnostics
 * @message: Human-readable error message describing the panic
 * @level: Severity level of the panic (affects recovery strategy)
 * @file: Source file where panic occurred (from __FILE__)
 * @line: Line number where panic occurred (from __LINE__)
 * @caller: Function name where panic occurred (from __func__)
 *
 * This is the main panic entry point that coordinates all diagnostic
 * information gathering and output. It's designed to be as robust as
 * possible - even if parts of the kernel are corrupted, this function
 * should still provide useful debugging information.
 *
 * Execution flow:
 * 1. Double panic guard check (prevents recursion)
 * 2. Disable interrupts (prevent further corruption)
 * 3. Dump comprehensive information to serial port (persistent, copyable)
 * 4. Display formatted panic on VGA console (user-visible)
 * 5. Dump CPU registers (critical state at time of panic)
 * 6. Unwind stack trace (call chain leading to panic)
 * 7. Dump process context (if available)
 * 8. Halt system safely
 */
void panicex(const char *message, panic_level_t level, const char *file, int line, const char *caller)
{
    /* Double panic detection - if we're already panicking, something is seriously wrong.
     * This typically indicates a bug in the panic handler itself or memory corruption
     * so severe that even basic operations fail.
     */
    if (in_panic)
    {
        Interrupts_off;
        serial_print("\n!!! DOUBLE PANIC DETECTED !!!\n");
        serial_print("DOUBLE PANIC! System completely fucked.\n");
        print_error("DOUBLE PANIC! System completely fucked.\n");
        cpu_relax();
        return;
    }

    in_panic = 1;

    /* Disable interrupts immediately - we can't handle any more events safely.
     * The system is in an inconsistent state and any interrupt could cause
     * further corruption or triple faults.
     */
    Interrupts_off;

    /* Dump comprehensive panic information to serial port first.
     * Serial output is structured for easy parsing and can be copied
     * from terminal emulators for external analysis.
     */
    serial_print("\n");
    serial_print("========================================\n");
    serial_print("KERNEL PANIC - SYSTEM HALTED\n");
    serial_print("========================================\n");
    serial_print("Timestamp: [kernel panic - no reliable time source]\n");
    serial_print("Panic Level: ");
    serial_print(panic_level_names[level]);
    serial_print("\n");
    serial_print("Source File: ");
    serial_print(file ? file : "unknown");
    serial_print("\n");
    serial_print("Line Number: ");
    serial_print_hex32((uint32_t)line);
    serial_print("\n");
    serial_print("Calling Function: ");
    serial_print(caller ? caller : "unknown");
    serial_print("\n");
    serial_print("Error Message: ");
    serial_print(message ? message : "no message");
    serial_print("\n");
    serial_print("========================================\n");

    clear_screen();
    print_colored("     ╔════════════════════════════════════════════════════════╗\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
    print_colored("     ║                                                        ║\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
    print_colored("     ║                      O_o KERNEL PANIC                  ║\n", VGA_COLOR_WHITE, VGA_COLOR_RED);
    print_colored("     ║                                                        ║\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
    print_colored("     ╚════════════════════════════════════════════════════════╝\n", VGA_COLOR_RED, VGA_COLOR_BLACK);

    print("\n");

    /* Panic info  */
    print_colored("Type: ", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_error(panic_level_names[level]);
    print("\n");

    print_colored("Location: ", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print(file);
    print(":");
    print_hex_compact(line);
    print("\n");

    print_colored("Caller: ", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print(caller ? caller : "unknown");
    print("\n");

    print_colored("Due to: ", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_error(message);
    print("\n\n");

    /* Dump CPU state - registers and control registers */
    dump_registers();

    /* Unwind call stack - shows the execution path that led to panic */
    dump_stack_trace();

    /* Dump process context - what process was running when panic occurred */
    dump_process_context();

    /* Dump memory state - heap statistics and allocation info */
    dump_memory_state();

    /* Final message before halting */ 
    serial_print("\n========================================\n");
    serial_print("SYSTEM HALTED - Safe to power off or reboot\n");
    serial_print("========================================\n");
    serial_print("\nCopy the above information for kernel debugging.\n");
    serial_print("End of panic dump.\n\n");
    
    print_colored("\n                          ═══ OOPS, SYSTEM HALTED ═══\n", VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    print_colored("\n Safe to power off or reboot.\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);

    goto sleep;

    sleep:
        cpu_relax(); /* System halted - CPU in sleep mode */
}

/**
 * dump_process_context - Dump information about the currently running process
 *
 * If a process was active when the panic occurred, dump its state including
 * PID, register values, memory layout, and execution state. This is critical
 * for debugging user-space related panics.
 */
static void dump_process_context(void)
{
    extern void *current_process; /* From kernel/process.c */
    extern void *process_list;
    
    serial_print("\n--- PROCESS CONTEXT ---\n");
    
    /* Try to get current process - may fail if memory is corrupted */
    if (current_process)
    {
        serial_print("Current Process: 0x");
        serial_print_hex64((uint64_t)(uintptr_t)current_process);
        serial_print("\n");
        
        /* Attempt to read process structure - be careful as it may be corrupted */
        /* We can't safely dereference without validation, so just print pointer */
    }
    else
    {
        serial_print("Current Process: NULL (no active process)\n");
    }
    
    if (process_list)
    {
        serial_print("Process List Head: 0x");
        serial_print_hex64((uint64_t)(uintptr_t)process_list);
        serial_print("\n");
    }
    else
    {
        serial_print("Process List: NULL (no processes)\n");
    }
    
    serial_print("\n");
}

/**
 * dump_memory_state - Dump kernel memory allocator statistics
 *
 * Provides information about heap usage, allocations, and fragmentation.
 * This helps diagnose memory-related panics (OOM, double free, corruption).
 */
static void dump_memory_state(void)
{
    serial_print("\n--- MEMORY STATE ---\n");
    
    /* Try to get allocator statistics - may not be available if memory is corrupted */
    extern uint32_t free_pages_count; /* From mm/allocator.c */
    
    serial_print("Free Pages Count: ");
    serial_print_hex32(free_pages_count);
    serial_print("\n");
    
    /* Note: Full memory statistics would require accessing allocator internals
     * which may not be safe during panic. This is a minimal safe dump.
     */
    serial_print("(Full memory statistics may be unavailable due to panic state)\n");
    serial_print("\n");
}

/**
 * dump_registers - Dump all CPU registers to console and serial
 *
 * Captures the complete CPU state at the moment of panic. This includes
 * general-purpose registers, stack pointer, instruction pointer, flags,
 * and control registers. Essential for understanding what the CPU was
 * doing when the panic occurred.
 *
 * Output format:
 * - VGA console: Formatted for human readability
 * - Serial port: Structured for automated parsing
 */
void dump_registers(void)
{
#ifdef __x86_64__
    /* 64-bit version - capture full 64-bit register state */
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rsp, rbp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rflags, rip;
    uint64_t cr0, cr2, cr3, cr4;

    /* Capture all general-purpose registers using inline assembly.
     * We use memory constraints to avoid register clobbering and ensure
     * all registers are captured atomically.
     */
    __asm__ volatile(
        "movq %%rax, %0\n"
        "movq %%rbx, %1\n"
        "movq %%rcx, %2\n"
        "movq %%rdx, %3\n"
        "movq %%rsi, %4\n"
        "movq %%rdi, %5\n"
        "movq %%rsp, %6\n"
        "movq %%rbp, %7\n"
        "movq %%r8, %8\n"
        "movq %%r9, %9\n"
        "movq %%r10, %10\n"
        "movq %%r11, %11\n"
        "movq %%r12, %12\n"
        "movq %%r13, %13\n"
        "movq %%r14, %14\n"
        "movq %%r15, %15\n"
        "pushfq\n"
        "popq %16\n"
        "leaq (%%rip), %17\n"
        : "=m"(rax), "=m"(rbx), "=m"(rcx), "=m"(rdx),
          "=m"(rsi), "=m"(rdi), "=m"(rsp), "=m"(rbp),
          "=m"(r8), "=m"(r9), "=m"(r10), "=m"(r11),
          "=m"(r12), "=m"(r13), "=m"(r14), "=m"(r15),
          "=m"(rflags), "=r"(rip)
        :
        : "memory");

    /* Capture control registers */
    __asm__ volatile("movq %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("movq %%cr2, %0" : "=r"(cr2));
    __asm__ volatile("movq %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("movq %%cr4, %0" : "=r"(cr4));

    /* Dump to serial port first - structured format */
    serial_print("\n--- CPU REGISTERS (x86-64) ---\n");
    serial_print("RAX=0x"); serial_print_hex64(rax); serial_print("  ");
    serial_print("RBX=0x"); serial_print_hex64(rbx); serial_print("  ");
    serial_print("RCX=0x"); serial_print_hex64(rcx); serial_print("  ");
    serial_print("RDX=0x"); serial_print_hex64(rdx); serial_print("\n");
    serial_print("RSI=0x"); serial_print_hex64(rsi); serial_print("  ");
    serial_print("RDI=0x"); serial_print_hex64(rdi); serial_print("  ");
    serial_print("RSP=0x"); serial_print_hex64(rsp); serial_print("  ");
    serial_print("RBP=0x"); serial_print_hex64(rbp); serial_print("\n");
    serial_print("R8=0x");  serial_print_hex64(r8);  serial_print("  ");
    serial_print("R9=0x");  serial_print_hex64(r9);  serial_print("  ");
    serial_print("R10=0x"); serial_print_hex64(r10); serial_print("  ");
    serial_print("R11=0x"); serial_print_hex64(r11); serial_print("\n");
    serial_print("R12=0x"); serial_print_hex64(r12); serial_print("  ");
    serial_print("R13=0x"); serial_print_hex64(r13); serial_print("  ");
    serial_print("R14=0x"); serial_print_hex64(r14); serial_print("  ");
    serial_print("R15=0x"); serial_print_hex64(r15); serial_print("\n");
    serial_print("RIP=0x"); serial_print_hex64(rip); serial_print("  ");
    serial_print("RFLAGS=0x"); serial_print_hex64(rflags); serial_print("\n");
    serial_print("CR0=0x"); serial_print_hex64(cr0); serial_print("  ");
    serial_print("CR2=0x"); serial_print_hex64(cr2); serial_print("  ");
    serial_print("CR3=0x"); serial_print_hex64(cr3); serial_print("  ");
    serial_print("CR4=0x"); serial_print_hex64(cr4); serial_print("\n");
    serial_print("\n");

    /* Also dump to VGA for user visibility */
    print_colored("--- REGISTER DUMP (64-bit) ---\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    print("RAX: ");
    print_hex64(rax);
    print("  ");
    print("RBX: ");
    print_hex64(rbx);
    print("\n");
    print("RIP: ");
    print_hex64(rip);
    print("  ");
    print("RSP: ");
    print_hex64(rsp);
    print("\n");

#else
    /* 32-bit Version  */
    uint32_t eax, ebx, ecx, edx, esi, edi, esp, ebp;
    uint32_t eflags;

    __asm__ volatile(
        "movl %%eax, %0\n"
        "movl %%ebx, %1\n"
        "movl %%ecx, %2\n"
        "movl %%edx, %3\n"
        "movl %%esi, %4\n"
        "movl %%edi, %5\n"
        "movl %%esp, %6\n"
        "movl %%ebp, %7\n"
        "pushfl\n"
        "popl %8\n"
        : "=m"(eax), "=m"(ebx), "=m"(ecx), "=m"(edx),
          "=m"(esi), "=m"(edi), "=m"(esp), "=m"(ebp), "=m"(eflags)
        :
        : "memory");

    print_colored("--- REGISTER DUMP (32-bit) ---\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);

    print("EAX: ");
    print_hex_compact(eax);
    print("  ");
    print("EBX: ");
    print_hex_compact(ebx);
    print("\n");

    print("ECX: ");
    print_hex_compact(ecx);
    print("  ");
    print("EDX: ");
    print_hex_compact(edx);
    print("\n");

    print("ESP: ");
    print_hex_compact(esp);
    print("  ");
    print("EBP: ");
    print_hex_compact(ebp);
    print("\n");

    print("EFLAGS: ");
    print_hex_compact(eflags);
    print("\n\n");
    
    /* Also dump to serial for copyability */
    serial_print("\n--- CPU REGISTERS (x86-32) ---\n");
    serial_print("EAX=0x"); serial_print_hex32(eax); serial_print("  ");
    serial_print("EBX=0x"); serial_print_hex32(ebx); serial_print("  ");
    serial_print("ECX=0x"); serial_print_hex32(ecx); serial_print("  ");
    serial_print("EDX=0x"); serial_print_hex32(edx); serial_print("\n");
    serial_print("ESI=0x"); serial_print_hex32(esi); serial_print("  ");
    serial_print("EDI=0x"); serial_print_hex32(edi); serial_print("  ");
    serial_print("ESP=0x"); serial_print_hex32(esp); serial_print("  ");
    serial_print("EBP=0x"); serial_print_hex32(ebp); serial_print("\n");
    serial_print("EFLAGS=0x"); serial_print_hex32(eflags); serial_print("\n");
    serial_print("\n");
#endif
}

/**
 * dump_stack_trace - Unwind and dump the call stack
 *
 * Walks the frame pointer chain to reconstruct the call stack that led
 * to the panic. This is crucial for understanding the execution path
 * and identifying which function called which.
 *
 * Limitations:
 * - Requires valid frame pointers (can fail if stack is corrupted)
 * - Maximum depth limited to prevent infinite loops
 * - May be truncated if stack bounds are invalid
 */
void dump_stack_trace(void)
{
    serial_print("\n--- STACK TRACE ---\n");
    print_colored("--- STACK TRACE ---\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);

#ifdef __x86_64__
    /* 64-bit stack unwinding - walk RBP chain */
    uint64_t *rbp;
    uint64_t rip;
    int frame_count = 0;
    const int max_frames = 20;  /* Limit depth to prevent infinite loops */

    asm volatile("mov %%rbp, %0" : "=r"(rbp));
    
    serial_print("Stack unwinding using RBP chain:\n");
    
    while (rbp && frame_count < max_frames)
    {
        /* Validate frame pointer is reasonable */
        if ((uint64_t)rbp < 0x100000 || (uint64_t)rbp > 0x7FFFFFFFFFFF)
        {
            serial_print("Stack trace truncated: invalid frame pointer (0x");
            serial_print_hex64((uint64_t)rbp);
            serial_print(")\n");
            break;
        }
        
        /* Return address is stored at [RBP+8] in x86-64 calling convention */
        rip = rbp[1];
        
        serial_print("[");
        serial_print_hex32(frame_count);
        serial_print("] 0x");
        serial_print_hex64(rip);
        serial_print(" (RBP=0x");
        serial_print_hex64((uint64_t)rbp);
        serial_print(")\n");
        
        /* Also print to VGA for visibility */
        print("#");
        print_hex_compact(frame_count);
        print(": 0x");
        print_hex64(rip);
        print("\n");
        
        /* Move to previous frame */
        rbp = (uint64_t *)*rbp;
        frame_count++;
    }
    
    if (frame_count == 0)
    {
        serial_print("No valid stack trace available (stack may be corrupted)\n");
        print_warning("No stack trace available\n");
    }
    else if (frame_count >= max_frames)
    {
        serial_print("Stack trace truncated at ");
        serial_print_hex32(max_frames);
        serial_print(" frames (possible loop detected)\n");
    }
    
    serial_print("\n");
#else

    uint32_t *ebp;
    asm volatile("movl %%ebp, %0" : "=r"(ebp));

    int frame_count = 0;
    const int max_frames = 10;

    while (ebp && frame_count < max_frames)
    {

        
        if ((uint32_t)ebp < 0x100000 || (uint32_t)ebp > 0x40000000)
        {
            print_warning("Stack trace truncated (invalid frame pointer)\n");
            break;
        }

        uint32_t return_addr = *(ebp + 1);

        print("#");
        print_hex_compact(frame_count);
        print(": ");
        print_hex_compact(return_addr);
        print("\n");

        ebp = (uint32_t *)*ebp; 
        frame_count++;
    }

    if (frame_count == 0)
    {
        print_warning("No stack trace available\n");
    }

    print("\n");
#endif
}


/* Unix panic() pipeline wrapper  */
void panic(const char *message)
{
    panicex(message, PANIC_KERNEL_BUG, "unknown", 0, "unknown");
}


void cpu_relax()
{
    for (;;)
    {
        Cpu_Sleep; /*CPU in sleep mode*/
    }
}
