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
#include <arch/common/arch_portable.h>

/*
 * Detect MinGW-w64 cross-compilation
 */
#if defined(__MINGW32__) || defined(__MINGW64__) || defined(_WIN32)
    #define MINGW_BUILD 1
#else
    #define MINGW_BUILD 0
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

[[maybe_unused]]void syscall_handler_c(void)
{
}
