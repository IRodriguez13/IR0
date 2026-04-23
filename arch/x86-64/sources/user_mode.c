#include <stdint.h>
#include <config.h>
#include <ir0/vga.h>
#include <ir0/oops.h>

// Detect MinGW-w64 cross-compilation
#if defined(__MINGW32__) || defined(__MINGW64__) || defined(_WIN32)
    #define MINGW_BUILD 1
#else
    #define MINGW_BUILD 0
#endif

// User mode transition function - WITH INTERRUPTS ENABLED
void jmp_ring3(void *entry_point)
{
#if MINGW_BUILD
    // MinGW-w64 stub - this function cannot work in Windows PE format
    // as it requires kernel mode execution
    (void)entry_point;
    panic("jmp_ring3 not supported in Windows build");
#else
    // Use stack within mapped 32MB region (safe area at 16MB)
    uintptr_t stack_top = 0x1000000 - 0x1000; // 16MB - 4KB
    
    // Switch to user mode - ENABLE INTERRUPTS for keyboard
    __asm__ volatile(
        "cli\n"
        "mov %[udsel], %%eax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "pushq %[udsel64]\n"
        "pushq %0\n"
        "pushfq\n"
        "pop %%rax\n"
        "or %[rflags_if], %%rax\n"
        "push %%rax\n"
        "pushq %[ucsel64]\n"
        "pushq %1\n"
        "iretq\n"
        :
        : "r"(stack_top), "r"((uintptr_t)entry_point),
          [udsel] "i" (USER_DATA_SEL), [udsel64] "i" ((uint64_t)USER_DATA_SEL),
          [rflags_if] "i" (RFLAGS_IF), [ucsel64] "i" ((uint64_t)USER_CODE_SEL)
        : "rax", "memory");

    panic("Returned from user mode unexpectedly");
#endif
}



[[maybe_unused]]void syscall_handler_c(void)
{
    // Syscall received
}