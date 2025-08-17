// interrupt/isr_handlers.c - ARREGLADO
#include <stdint.h>
#include <stddef.h>
#include <print.h>
#include <panic/panic.h>
#include <string.h>
#include "../arch/common/arch_interface.h"
#include "../arch/common/idt.h"
#include "../kernel/syscalls/syscalls.h"
#include "../kernel/scheduler/scheduler.h"

// ===============================================================================
// TYPE DEFINITIONS
// ===============================================================================

typedef struct 
{
#ifdef __x86_64__
    // General purpose registers (64-bit)
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp, rsp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rip, rflags;
    
    // Segment registers
    uint64_t cs, ds, es, fs, gs, ss;
    
    // Control registers
    uint64_t cr0, cr2, cr3, cr4;
    
    // Debug registers
    uint64_t dr0, dr1, dr2, dr3, dr6, dr7;
#else
    // General purpose registers (32-bit)
    uint32_t eax, ebx, ecx, edx;
    uint32_t esi, edi, ebp, esp;
    uint32_t eip, eflags;
    
    // Segment registers
    uint32_t cs, ds, es, fs, gs, ss;
    
    // Control registers
    uint32_t cr0, cr2, cr3, cr4;
    
    // Debug registers
    uint32_t dr0, dr1, dr2, dr3, dr6, dr7;
#endif
} interrupt_context_t;

// ===============================================================================
// FUNCTION DECLARATIONS
// ===============================================================================

// Context management
void isr_save_context(interrupt_context_t *context);
void isr_restore_context(interrupt_context_t *context);

// Exception handlers
void isr_division_error(interrupt_context_t *context, uint32_t error_code);
void isr_debug_exception(interrupt_context_t *context, uint32_t error_code);
void isr_nmi_interrupt(interrupt_context_t *context, uint32_t error_code);
void isr_breakpoint(interrupt_context_t *context, uint32_t error_code);
void isr_overflow(interrupt_context_t *context, uint32_t error_code);
void isr_bound_range_exceeded(interrupt_context_t *context, uint32_t error_code);
void isr_invalid_opcode(interrupt_context_t *context, uint32_t error_code);
void isr_device_not_available(interrupt_context_t *context, uint32_t error_code);
void isr_double_fault(interrupt_context_t *context, uint32_t error_code);
void isr_coprocessor_segment_overrun(interrupt_context_t *context, uint32_t error_code);
void isr_invalid_tss(interrupt_context_t *context, uint32_t error_code);
void isr_segment_not_present(interrupt_context_t *context, uint32_t error_code);
void isr_stack_segment_fault(interrupt_context_t *context, uint32_t error_code);
void isr_general_protection_fault(interrupt_context_t *context, uint32_t error_code);
void isr_page_fault_handler(interrupt_context_t *context, uint32_t error_code);
void isr_floating_point_error(interrupt_context_t *context, uint32_t error_code);
void isr_alignment_check(interrupt_context_t *context, uint32_t error_code);
void isr_machine_check(interrupt_context_t *context, uint32_t error_code);
void isr_simd_floating_point_exception(interrupt_context_t *context, uint32_t error_code);
void isr_virtualization_exception(interrupt_context_t *context, uint32_t error_code);
void isr_control_protection_exception(interrupt_context_t *context, uint32_t error_code);

// Hardware interrupt handlers
void isr_timer_interrupt(interrupt_context_t *context, uint32_t error_code);
void isr_keyboard_interrupt(interrupt_context_t *context, uint32_t error_code);
void isr_system_call(interrupt_context_t *context, uint32_t error_code);
void isr_unknown_interrupt(interrupt_context_t *context, uint32_t error_code, uint32_t isr_number);

// Utility functions
void isr_send_eoi(uint8_t irq);
void isr_log_exception(const char *exception_name, interrupt_context_t *context, uint32_t error_code);
int isr_handle_page_fault(uint32_t fault_address, uint32_t error_code, interrupt_context_t *context);
void isr_update_system_time(void);
void isr_handle_keyboard_input(uint8_t scan_code);
int64_t isr_handle_system_call(uint32_t syscall_number, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5);

// ===============================================================================
// ERROR CODES
// ===============================================================================

#define ENOSYS 38

// ===============================================================================
// MACROS FOR ARCHITECTURE-SPECIFIC ACCESS
// ===============================================================================

#ifdef __x86_64__
#define GET_INSTRUCTION_POINTER(context) ((context)->rip)
#else
#define GET_INSTRUCTION_POINTER(context) ((context)->eip)
#endif

// ===============================================================================
// INTERRUPT SERVICE ROUTINE HANDLERS IMPLEMENTATION
// ===============================================================================

void isr_handler_common(uint32_t isr_number, uint32_t error_code)
{
    // Save current context
    interrupt_context_t context;
    isr_save_context(&context);
    
    // Handle specific interrupt
    switch (isr_number) {
        case 0: // Division by zero
            isr_division_error(&context, error_code);
            break;
        case 1: // Debug exception
            isr_debug_exception(&context, error_code);
            break;
        case 2: // Non-maskable interrupt
            isr_nmi_interrupt(&context, error_code);
            break;
        case 3: // Breakpoint
            isr_breakpoint(&context, error_code);
            break;
        case 4: // Overflow
            isr_overflow(&context, error_code);
            break;
        case 5: // Bound range exceeded
            isr_bound_range_exceeded(&context, error_code);
            break;
        case 6: // Invalid opcode
            isr_invalid_opcode(&context, error_code);
            break;
        case 7: // Device not available
            isr_device_not_available(&context, error_code);
            break;
        case 8: // Double fault
            isr_double_fault(&context, error_code);
            break;
        case 9: // Coprocessor segment overrun
            isr_coprocessor_segment_overrun(&context, error_code);
            break;
        case 10: // Invalid TSS
            isr_invalid_tss(&context, error_code);
            break;
        case 11: // Segment not present
            isr_segment_not_present(&context, error_code);
            break;
        case 12: // Stack segment fault
            isr_stack_segment_fault(&context, error_code);
            break;
        case 13: // General protection fault
            isr_general_protection_fault(&context, error_code);
            break;
        case 14: // Page fault
            isr_page_fault_handler(&context, error_code);
            break;
        case 16: // Floating point error
            isr_floating_point_error(&context, error_code);
            break;
        case 17: // Alignment check
            isr_alignment_check(&context, error_code);
            break;
        case 18: // Machine check
            isr_machine_check(&context, error_code);
            break;
        case 19: // SIMD floating point exception
            isr_simd_floating_point_exception(&context, error_code);
            break;
        case 20: // Virtualization exception
            isr_virtualization_exception(&context, error_code);
            break;
        case 21: // Control protection exception
            isr_control_protection_exception(&context, error_code);
            break;
        case 32: // Timer interrupt
            isr_timer_interrupt(&context, error_code);
            break;
        case 33: // Keyboard interrupt
            isr_keyboard_interrupt(&context, error_code);
            break;
        case 128: // System call
            isr_system_call(&context, error_code);
            break;
        default:
            isr_unknown_interrupt(&context, error_code, isr_number);
            break;
    }
    
    // Restore context and return
    isr_restore_context(&context);
}

// ===============================================================================
// EXCEPTION HANDLERS
// ===============================================================================

void isr_division_error(interrupt_context_t *context, uint32_t error_code)
{
    (void)error_code;
    
    print_error("Division by zero exception");
    print_error("EIP/RIP: 0x");
    print_hex64(GET_INSTRUCTION_POINTER(context));
    print_error("\n");
    
    // Log the exception
    isr_log_exception("Division by zero", context, error_code);
    
    // In a real kernel, we might try to recover or terminate the process
    // For now, just continue execution
}

void isr_debug_exception(interrupt_context_t *context, uint32_t error_code)
{
    (void)context;
    (void)error_code;
    
    // Debug exceptions are usually handled by debuggers
    // For now, just log and continue
    isr_log_exception("Debug exception", context, error_code);
}

void isr_nmi_interrupt(interrupt_context_t *context, uint32_t error_code)
{
    (void)context;
    (void)error_code;
    
    // Non-maskable interrupts are critical
    print_warning("Non-maskable interrupt received");
    isr_log_exception("NMI interrupt", context, error_code);
}

void isr_breakpoint(interrupt_context_t *context, uint32_t error_code)
{
    (void)error_code;
    
    print("Breakpoint hit at EIP/RIP: 0x");
    print_hex64(GET_INSTRUCTION_POINTER(context));
    print("\n");
    
    isr_log_exception("Breakpoint", context, error_code);
}

void isr_overflow(interrupt_context_t *context, uint32_t error_code)
{
    (void)context;
    (void)error_code;
    
    print_error("Overflow exception");
    isr_log_exception("Overflow", context, error_code);
}

void isr_bound_range_exceeded(interrupt_context_t *context, uint32_t error_code)
{
    (void)context;
    (void)error_code;
    
    print_error("Bound range exceeded exception");
    isr_log_exception("Bound range exceeded", context, error_code);
}

void isr_invalid_opcode(interrupt_context_t *context, uint32_t error_code)
{
    (void)error_code;
    
    print_error("Invalid opcode exception");
    print_error("EIP/RIP: 0x");
    print_hex64(GET_INSTRUCTION_POINTER(context));
    print_error("\n");
    
    isr_log_exception("Invalid opcode", context, error_code);
}

void isr_device_not_available(interrupt_context_t *context, uint32_t error_code)
{
    (void)context;
    (void)error_code;
    
    print_warning("Device not available exception");
    isr_log_exception("Device not available", context, error_code);
}

void isr_double_fault(interrupt_context_t *context, uint32_t error_code)
{
    (void)context;
    (void)error_code;
    
    print_error("Double fault exception - System may be unstable");
    isr_log_exception("Double fault", context, error_code);
    
    // Double faults are serious - consider system reset
    // arch_reset_system();
}

void isr_coprocessor_segment_overrun(interrupt_context_t *context, uint32_t error_code)
{
    (void)context;
    (void)error_code;
    
    print_error("Coprocessor segment overrun");
    isr_log_exception("Coprocessor segment overrun", context, error_code);
}

void isr_invalid_tss(interrupt_context_t *context, uint32_t error_code)
{
    (void)context;
    (void)error_code;
    
    print_error("Invalid TSS exception");
    isr_log_exception("Invalid TSS", context, error_code);
}

void isr_segment_not_present(interrupt_context_t *context, uint32_t error_code)
{
    (void)context;
    (void)error_code;
    
    print_error("Segment not present exception");
    isr_log_exception("Segment not present", context, error_code);
}

void isr_stack_segment_fault(interrupt_context_t *context, uint32_t error_code)
{
    (void)context;
    (void)error_code;
    
    print_error("Stack segment fault");
    isr_log_exception("Stack segment fault", context, error_code);
}

void isr_general_protection_fault(interrupt_context_t *context, uint32_t error_code)
{
    print_error("General protection fault");
    print_error("Error code: 0x");
    print_hex64(error_code);
    print_error(" EIP/RIP: 0x");
    print_hex64(GET_INSTRUCTION_POINTER(context));
    print_error("\n");
    
    isr_log_exception("General protection fault", context, error_code);
}

void isr_page_fault_handler(interrupt_context_t *context, uint32_t error_code)
{
#ifdef __x86_64__
    uint64_t fault_address;
    
    // Get the faulting address for x86-64
    __asm__ volatile("mov %%cr2, %0" : "=r" (fault_address) : : "memory");
#else
    uint32_t fault_address;
    
    // Get the faulting address for x86-32
    __asm__ volatile("mov %%cr2, %0" : "=r" (fault_address));
#endif
    
    print_error("Page fault");
    print_error("Fault address: 0x");
    print_hex64(fault_address);
    print_error(" Error code: 0x");
    print_hex64(error_code);
    print_error(" EIP/RIP: 0x");
    print_hex64(GET_INSTRUCTION_POINTER(context));
    print_error("\n");
    
    // Handle page fault
    if (isr_handle_page_fault(fault_address, error_code, context) != 0) 
    {
        // Page fault could not be handled
        isr_log_exception("Unhandled page fault", context, error_code);
    }
}

void isr_floating_point_error(interrupt_context_t *context, uint32_t error_code)
{
    (void)context;
    (void)error_code;
    
    print_error("Floating point error");
    isr_log_exception("Floating point error", context, error_code);
}

void isr_alignment_check(interrupt_context_t *context, uint32_t error_code)
{
    (void)context;
    (void)error_code;
    
    print_error("Alignment check exception");
    isr_log_exception("Alignment check", context, error_code);
}

void isr_machine_check(interrupt_context_t *context, uint32_t error_code)
{
    (void)context;
    (void)error_code;
    
    print_error("Machine check exception - Hardware error detected");
    isr_log_exception("Machine check", context, error_code);
    
    // Machine checks indicate hardware problems
    // Consider system shutdown
}

void isr_simd_floating_point_exception(interrupt_context_t *context, uint32_t error_code)
{
    (void)context;
    (void)error_code;
    
    print_error("SIMD floating point exception");
    isr_log_exception("SIMD floating point exception", context, error_code);
}

void isr_virtualization_exception(interrupt_context_t *context, uint32_t error_code)
{
    (void)context;
    (void)error_code;
    
    print_error("Virtualization exception");
    isr_log_exception("Virtualization exception", context, error_code);
}

void isr_control_protection_exception(interrupt_context_t *context, uint32_t error_code)
{
    (void)context;
    (void)error_code;
    
    print_error("Control protection exception");
    isr_log_exception("Control protection exception", context, error_code);
}

// ===============================================================================
// HARDWARE INTERRUPT HANDLERS
// ===============================================================================

void isr_timer_interrupt(interrupt_context_t *context, uint32_t error_code)
{
    (void)context;
    (void)error_code;
    
    // Update system time
    isr_update_system_time();
    
    // Handle scheduler tick
    // TODO: Call scheduler_tick() when scheduler is ready
    // if (scheduler_is_running()) {
    //     scheduler_tick();
    // }
    
    // Send EOI to PIC
    isr_send_eoi(0);
}

void isr_keyboard_interrupt(interrupt_context_t *context, uint32_t error_code)
{
    (void)context;
    (void)error_code;
    
    // Read keyboard scan code
    uint8_t scan_code = inb(0x60);
    
    // Handle keyboard input
    isr_handle_keyboard_input(scan_code);
    
    // Send EOI to PIC
    isr_send_eoi(1);
}

void isr_system_call(interrupt_context_t *context, uint32_t error_code)
{
    (void)error_code;
    
#ifdef __x86_64__
    // Get system call number and arguments for x86-64
    uint32_t syscall_number = (uint32_t)context->rax;
    uint32_t arg1 = (uint32_t)context->rbx;
    uint32_t arg2 = (uint32_t)context->rcx;
    uint32_t arg3 = (uint32_t)context->rdx;
    uint32_t arg4 = (uint32_t)context->rsi;
    uint32_t arg5 = (uint32_t)context->rdi;
    
    // Handle system call
    int64_t result = isr_handle_system_call(syscall_number, arg1, arg2, arg3, arg4, arg5);
    
    // Return result in RAX
    context->rax = result;
#else
    // Get system call number and arguments for x86-32
    uint32_t syscall_number = context->eax;
    uint32_t arg1 = context->ebx;
    uint32_t arg2 = context->ecx;
    uint32_t arg3 = context->edx;
    uint32_t arg4 = context->esi;
    uint32_t arg5 = context->edi;
    
    // Handle system call
    int64_t result = isr_handle_system_call(syscall_number, arg1, arg2, arg3, arg4, arg5);
    
    // Return result in EAX
    context->eax = (uint32_t)result;
#endif
}

void isr_unknown_interrupt(interrupt_context_t *context, uint32_t error_code, uint32_t isr_number)
{
    print_error("Unknown interrupt: ");
    print_uint64(isr_number);
    print_error(" Error code: 0x");
    print_hex64(error_code);
    print_error(" EIP/RIP: 0x");
    print_hex64(GET_INSTRUCTION_POINTER(context));
    print_error("\n");
    
    isr_log_exception("Unknown interrupt", context, error_code);
}

// ===============================================================================
// UTILITY FUNCTIONS
// ===============================================================================

void isr_save_context(interrupt_context_t *context)
{
    if (!context) {
        return;
    }
    
#ifdef __x86_64__
    // Save general purpose registers for x86-64
    __asm__ volatile(
        "mov %%rax, %0\n"
        "mov %%rbx, %1\n"
        "mov %%rcx, %2\n"
        "mov %%rdx, %3\n"
        "mov %%rsi, %4\n"
        "mov %%rdi, %5\n"
        "mov %%rbp, %6\n"
        "mov %%rsp, %7\n"
        : "=m" (context->rax), "=m" (context->rbx), "=m" (context->rcx), "=m" (context->rdx),
          "=m" (context->rsi), "=m" (context->rdi), "=m" (context->rbp), "=m" (context->rsp)
        :
        : "memory"
    );
#else
    // Save general purpose registers for x86-32
    __asm__ volatile(
        "mov %%eax, %0\n"
        "mov %%ebx, %1\n"
        "mov %%ecx, %2\n"
        "mov %%edx, %3\n"
        "mov %%esi, %4\n"
        "mov %%edi, %5\n"
        "mov %%ebp, %6\n"
        "mov %%esp, %7\n"
        : "=m" (context->eax), "=m" (context->ebx), "=m" (context->ecx), "=m" (context->edx),
          "=m" (context->esi), "=m" (context->edi), "=m" (context->ebp), "=m" (context->esp)
        :
        : "memory"
    );
#endif
}

void isr_restore_context(interrupt_context_t *context)
{
    if (!context) {
        return;
    }
    
#ifdef __x86_64__
    // Restore general purpose registers for x86-64
    __asm__ volatile("mov %0, %%rax" : : "m" (context->rax) : "rax");
    __asm__ volatile("mov %0, %%rbx" : : "m" (context->rbx) : "rbx");
    __asm__ volatile("mov %0, %%rcx" : : "m" (context->rcx) : "rcx");
    __asm__ volatile("mov %0, %%rdx" : : "m" (context->rdx) : "rdx");
    __asm__ volatile("mov %0, %%rsi" : : "m" (context->rsi) : "rsi");
    __asm__ volatile("mov %0, %%rdi" : : "m" (context->rdi) : "rdi");
    __asm__ volatile("mov %0, %%rbp" : : "m" (context->rbp) : "rbp");
    __asm__ volatile("mov %0, %%rsp" : : "m" (context->rsp) : "rsp");
#else
    // Restore general purpose registers for x86-32
    __asm__ volatile("mov %0, %%eax" : : "m" (context->eax) : "eax");
    __asm__ volatile("mov %0, %%ebx" : : "m" (context->ebx) : "ebx");
    __asm__ volatile("mov %0, %%ecx" : : "m" (context->ecx) : "ecx");
    __asm__ volatile("mov %0, %%edx" : : "m" (context->edx) : "edx");
    __asm__ volatile("mov %0, %%esi" : : "m" (context->esi) : "esi");
    __asm__ volatile("mov %0, %%edi" : : "m" (context->edi) : "edi");
    __asm__ volatile("mov %0, %%ebp" : : "m" (context->ebp) : "ebp");
#endif
}

void isr_send_eoi(uint8_t irq)
{
    if (irq >= 8) {
        // Send EOI to slave PIC
        outb(0xA0, 0x20);
    }
    // Send EOI to master PIC
    outb(0x20, 0x20);
}

void isr_log_exception(const char *exception_name, interrupt_context_t *context, uint32_t error_code)
{
    // TODO: Implement exception logging
    (void)exception_name;
    (void)context;
    (void)error_code;
}

int isr_handle_page_fault(uint32_t fault_address, uint32_t error_code, interrupt_context_t *context)
{
    // TODO: Implement page fault handling
    (void)fault_address;
    (void)error_code;
    (void)context;
    
    return -1; // Could not handle
}

void isr_update_system_time(void)
{
    // TODO: Implement system time update
    static uint32_t tick_count = 0;
    tick_count++;
}

void isr_handle_keyboard_input(uint8_t scan_code)
{
    // TODO: Implement keyboard input handling
    (void)scan_code;
}

int64_t isr_handle_system_call(uint32_t syscall_number, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5)
{
    // TODO: Implement system call handling
    (void)arg1;
    (void)arg2;
    (void)arg3;
    (void)arg4;
    (void)arg5;
    
    switch (syscall_number) {
        case 1: // sys_exit
            return sys_exit((int)arg1);
        case 2: // sys_fork
            return sys_fork();
        case 3: // sys_read
            return sys_read((int)arg1, (void *)arg2, (size_t)arg3);
        case 4: // sys_write
            return sys_write((int)arg1, (const void *)arg2, (size_t)arg3);
        case 5: // sys_open
            return sys_open((const char *)arg1, (int)arg2, (int)arg3);
        case 6: // sys_close
            return sys_close((int)arg1);
        default:
            return -ENOSYS; // Function not implemented
    }
}

// ===============================================================================
// I/O FUNCTIONS
// ===============================================================================

// ===============================================================================
// MISSING INTERRUPT HANDLERS
// ===============================================================================

void default_interrupt_handler(void)
{
    // Default handler for unhandled interrupts
    // TODO: Implement proper default handling
    print("Default interrupt handler called\n");
}

void time_handler(void)
{
    // Simple timer interrupt handler - just send EOI
    isr_send_eoi(0); // Timer is IRQ 0
}