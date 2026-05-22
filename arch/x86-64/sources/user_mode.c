/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: user_mode.c
 * Description: x86-64 transition to ring 3 (implements arch_switch_to_user declared in arch_portable.h)
 */

#include <stdint.h>
#include <config.h>
#include <ir0/vga.h>
#include <ir0/oops.h>
#include <kernel/scheduler/task.h>
#include <arch/common/arch_portable.h>

/*
 * Detect MinGW-w64 cross-compilation
 */
#if defined(__MINGW32__) || defined(__MINGW64__) || defined(_WIN32)
    #define MINGW_BUILD 1
#else
    #define MINGW_BUILD 0
#endif

#if !MINGW_BUILD
extern void arch_switch_to_user_task_asm(const task_t *task);
#endif

void arch_switch_to_user(arch_addr_t entry, arch_addr_t stack_top)
{
#if MINGW_BUILD
    (void)entry;
    (void)stack_top;
    panic("arch_switch_to_user not supported in Windows build");
#else
    uintptr_t uentry = (uintptr_t)entry;
    uintptr_t ursp = (uintptr_t)stack_top;

    /*
     * iretq to user code with user data segments; RFLAGS_IF set so device IRQs work.
     */
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
        : "r"(ursp), "r"(uentry),
          [udsel] "i"(USER_DATA_SEL), [udsel64] "i"((uint64_t)USER_DATA_SEL),
          [rflags_if] "i"(RFLAGS_IF), [ucsel64] "i"((uint64_t)USER_CODE_SEL)
        : "rax", "memory");

    panic("Returned from user mode unexpectedly");
#endif
}

void arch_switch_to_user_task(const task_t *task)
{
#if MINGW_BUILD
    (void)task;
    panic("arch_switch_to_user_task not supported in Windows build");
#else
    if (!task)
        panic("arch_switch_to_user_task: null task");

    /* FASE 12: pre-iretq snapshot of intended user-mode state */
    serial_print("FASE12 pre_iretq pid=");
    serial_print_hex32((uint32_t)task->pid);
    serial_print(" rip=");
    serial_print_hex64(task->rip);
    serial_print(" rsp=");
    serial_print_hex64(task->rsp);
    serial_print(" rax=");
    serial_print_hex64(task->rax);
    serial_print(" rbx=");
    serial_print_hex64(task->rbx);
    serial_print("\n");
    serial_print("FASE12 pre_iretq cs=");
    serial_print_hex64((uint64_t)task->cs);
    serial_print(" ss=");
    serial_print_hex64((uint64_t)task->ss);
    serial_print(" rflags=");
    serial_print_hex64(task->rflags);
    serial_print(" cr3=");
    serial_print_hex64(task->cr3);
    serial_print("\n");

    serial_print("FASE32_RETURN rsp_used=");
    serial_print_hex64(task->rsp);
    serial_print("\n");

    arch_switch_to_user_task_asm(task);
    panic("Returned from arch_switch_to_user_task unexpectedly");
#endif
}

#define MSR_IA32_FS_BASE 0xC0000100U

void arch_set_fs_base(uint64_t base)
{
#if MINGW_BUILD
    (void)base;
#else
    uint32_t lo = (uint32_t)(base & 0xFFFFFFFFU);
    uint32_t hi = (uint32_t)(base >> 32);

    __asm__ volatile("wrmsr" : : "c"(MSR_IA32_FS_BASE), "a"(lo), "d"(hi) : "memory");
#endif
}

uint64_t arch_get_fs_base(void)
{
#if MINGW_BUILD
    return 0;
#else
    uint32_t lo;
    uint32_t hi;

    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(MSR_IA32_FS_BASE));
    return ((uint64_t)hi << 32) | (uint64_t)lo;
#endif
}

[[maybe_unused]]void syscall_handler_c(void)
{
}

/*
 * FASE 24: one-shot log emitted from syscall_insn_entry_asm to confirm the
 * dedicated kernel syscall stack swap is in effect and balanced.
 */
extern uint64_t fase24_user_rsp_snap;
extern uint64_t fase24_kernel_rsp_snap;
extern uint64_t fase24_rsp_pre_sysret;
extern uint64_t fase25_user_rsp_saved;
extern uint64_t fase25_rsp_before_restore;
extern uint64_t fase25_rsp_after_restore;
extern uint64_t fase25_rcx_before_sysret;
extern uint64_t fase25_r11_before_sysret;
extern uint64_t fase27_rax_before_sysret;
extern uint64_t fase27_rsp_before_sysret;
extern uint64_t fase27_rcx_before_sysret;
extern uint64_t fase27_r11_before_sysret;
extern uint64_t fase27_rdx_before_sysret;
extern uint64_t fase27_rsi_before_sysret;
extern uint64_t fase27_rdi_before_sysret;
extern uint64_t fase27_r8_before_sysret;
extern uint64_t fase27_r9_before_sysret;
extern uint64_t fase27_r10_before_sysret;
extern uint64_t kernel_syscall_stack_top;

#if !MINGW_BUILD
void fase24_log_stack_once(void)
{
    static int logged = 0;

    if (logged)
        return;
    logged = 1;

    serial_print("FASE24 user_rsp_before=");
    serial_print_hex64(fase24_user_rsp_snap);
    serial_print(" kernel_rsp_after_switch=");
    serial_print_hex64(fase24_kernel_rsp_snap);
    serial_print(" rsp_before_sysret=");
    serial_print_hex64(fase24_rsp_pre_sysret);
    serial_print(" kstack_top=");
    serial_print_hex64(kernel_syscall_stack_top);
    serial_print("\nFASE25 user_rsp_saved=");
    serial_print_hex64(fase25_user_rsp_saved);
    serial_print(" rsp_before_restore=");
    serial_print_hex64(fase25_rsp_before_restore);
    serial_print(" rsp_after_restore=");
    serial_print_hex64(fase25_rsp_after_restore);
    serial_print(" rcx_before_sysret=");
    serial_print_hex64(fase25_rcx_before_sysret);
    serial_print(" r11_before_sysret=");
    serial_print_hex64(fase25_r11_before_sysret);
    serial_print(" delta_rsp=");
    serial_print_hex64(fase25_rsp_before_restore - fase24_kernel_rsp_snap);
    serial_print("\nFASE27 post_sysret_emit rax=");
    serial_print_hex64(fase27_rax_before_sysret);
    serial_print(" rcx=");
    serial_print_hex64(fase27_rcx_before_sysret);
    serial_print(" r11=");
    serial_print_hex64(fase27_r11_before_sysret);
    serial_print(" rsp=");
    serial_print_hex64(fase27_rsp_before_sysret);
    serial_print("\nFASE27 post_sysret_emit rdx=");
    serial_print_hex64(fase27_rdx_before_sysret);
    serial_print(" rsi=");
    serial_print_hex64(fase27_rsi_before_sysret);
    serial_print(" rdi=");
    serial_print_hex64(fase27_rdi_before_sysret);
    serial_print(" r8=");
    serial_print_hex64(fase27_r8_before_sysret);
    serial_print(" r9=");
    serial_print_hex64(fase27_r9_before_sysret);
    serial_print(" r10=");
    serial_print_hex64(fase27_r10_before_sysret);
    serial_print("\n");
}
#endif
