/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: debug_dump.c
 * Description: ISA-local panic register/stack dump (x86).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/oops.h>
#include <ir0/vga.h>
#include <ir0/ktm/klog.h>
#include <stdint.h>

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
    klog_debug_fmt("KERN",
                   "--- CPU REGISTERS (x86-64) --- "
                   "RAX=0x%llx RBX=0x%llx RCX=0x%llx RDX=0x%llx "
                   "RSI=0x%llx RDI=0x%llx RSP=0x%llx RBP=0x%llx "
                   "R8=0x%llx R9=0x%llx R10=0x%llx R11=0x%llx "
                   "R12=0x%llx R13=0x%llx R14=0x%llx R15=0x%llx "
                   "RIP=0x%llx RFLAGS=0x%llx CR0=0x%llx CR2=0x%llx CR3=0x%llx CR4=0x%llx",
                   (unsigned long long)rax, (unsigned long long)rbx,
                   (unsigned long long)rcx, (unsigned long long)rdx,
                   (unsigned long long)rsi, (unsigned long long)rdi,
                   (unsigned long long)rsp, (unsigned long long)rbp,
                   (unsigned long long)r8, (unsigned long long)r9,
                   (unsigned long long)r10, (unsigned long long)r11,
                   (unsigned long long)r12, (unsigned long long)r13,
                   (unsigned long long)r14, (unsigned long long)r15,
                   (unsigned long long)rip, (unsigned long long)rflags,
                   (unsigned long long)cr0, (unsigned long long)cr2,
                   (unsigned long long)cr3, (unsigned long long)cr4);

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
    klog_debug_fmt("KERN",
                   "--- CPU REGISTERS (x86-32) --- "
                   "EAX=0x%x EBX=0x%x ECX=0x%x EDX=0x%x "
                   "ESI=0x%x EDI=0x%x ESP=0x%x EBP=0x%x EFLAGS=0x%x",
                   (unsigned)eax, (unsigned)ebx, (unsigned)ecx, (unsigned)edx,
                   (unsigned)esi, (unsigned)edi, (unsigned)esp, (unsigned)ebp,
                   (unsigned)eflags);
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
    klog_debug("KERN", "\n--- STACK TRACE ---\n");
    print_colored("--- STACK TRACE ---\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);

#ifdef __x86_64__
    /* 64-bit stack unwinding - walk RBP chain */
    uint64_t *rbp;
    uint64_t rip;
    int frame_count = 0;
    const int max_frames = 20;  /* Limit depth to prevent infinite loops */

    asm volatile("mov %%rbp, %0" : "=r"(rbp));

    klog_debug("KERN", "Stack unwinding using RBP chain:\n");

    while (rbp && frame_count < max_frames)
    {
        /*
         * Never chase frame pointers into userspace — that dereferences user
         * addresses from the panic handler and causes a secondary fault.
         */
        if ((uint64_t)rbp < 0x100000ULL ||
            ((uint64_t)rbp >= 0x00400000ULL &&
             (uint64_t)rbp <= 0x00007FFFFFFFFFFFULL))
        {
            klog_debug_fmt("KERN", "Stack trace truncated: invalid frame pointer (0x%llx)\n", (unsigned long long)((uint64_t)rbp));
            break;
        }

        /* Return address is stored at [RBP+8] in x86-64 calling convention */
        rip = rbp[1];

        klog_debug_fmt("KERN", "[%x] 0x%llx (RBP=0x%llx)\n", (unsigned)(frame_count), (unsigned long long)(rip), (unsigned long long)((uint64_t)rbp));

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
        klog_debug("KERN", "No valid stack trace available (stack may be corrupted)\n");
        print_warning("No stack trace available\n");
    }
    else if (frame_count >= max_frames)
    {
        klog_debug_fmt("KERN", "Stack trace truncated at %x frames (possible loop detected)",
                       (unsigned)(max_frames));
    }
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
