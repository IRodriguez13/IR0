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
#include <ir0/serial_io.h>
#include <sched/task.h>
#include <arch/common/arch_portable.h>

#define USER_CANON_MIN 0x00400000ULL
#define USER_CANON_MAX 0x00007FFFFFFFFFFFULL

static int arch_user_va_canonical(uint64_t va)
{
    return va >= USER_CANON_MIN && va <= USER_CANON_MAX;
}

static void arch_audit_iret_frame(const task_t *task)
{
    serial_print("[WAIT_EXIT_AUDIT][IRET_CHECK] rip=");
    serial_print_hex64(task->rip);
    serial_print(" rsp=");
    serial_print_hex64(task->rsp);
    serial_print(" cs=");
    serial_print_hex64((uint64_t)task->cs);
    serial_print(" ss=");
    serial_print_hex64((uint64_t)task->ss);
    serial_print(" rflags=");
    serial_print_hex64(task->rflags);
    serial_print("\n");

    if ((task->cs & 0xFFFFU) != (uint16_t)USER_CODE_SEL ||
        (task->ss & 0xFFFFU) != (uint16_t)USER_DATA_SEL)
    {
        serial_print("[WAIT_EXIT_AUDIT][CLASSIFY] PARENT_IRET_FRAME_BAD_CS_SS\n");
        panic("arch_switch_to_user_task: invalid CS/SS for ring3 iretq");
    }
    if (!arch_user_va_canonical(task->rip))
    {
        serial_print("[WAIT_EXIT_AUDIT][CLASSIFY] PARENT_IRET_FRAME_BAD_RIP\n");
        panic("arch_switch_to_user_task: invalid RIP for ring3 iretq");
    }
    if (!arch_user_va_canonical(task->rsp))
    {
        serial_print("[WAIT_EXIT_AUDIT][CLASSIFY] PARENT_IRET_FRAME_BAD_RSP\n");
        panic("arch_switch_to_user_task: invalid RSP for ring3 iretq");
    }
    serial_print("[WAIT_EXIT_AUDIT][CLASSIFY] RETURN_TO_PARENT_OK\n");
}

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

    arch_audit_iret_frame(task);
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

#if !MINGW_BUILD
void fase24_log_stack_once(void)
{
    /* Temporary trace hook intentionally left as no-op. */
}
#endif
