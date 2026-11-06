/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: user_mode.c
 * Description: EL1→EL0 entry (facade: switch_to_user / switch_to_user_task).
 */

#include <arch/common/arch_portable.h>
#include <ir0/oops.h>
#include <ir0/paging.h>
#include <ir0/process.h>
#include <ir0/arch_task.h>
#include <ir0/tls.h>
#include <sched/task.h>

#include <stdint.h>

/*
 * Saved Program Status at exception return to AArch64 EL0 (EL0t).
 * Bits [9:6] DAIF: clear so IRQ/FIQ are not masked at EL0 (device IRQs after eret).
 * M[3:0]=0: EL0t (AArch64). Matches the early exception path (SPSR_MODE_EL0T).
 */
#define SPSR_EL1_EL0T_AARCH64_DAIF_UNMASKED 0x0UL

void switch_to_user(arch_addr_t entry, arch_addr_t stack_top)
{
	uint64_t elr = (uint64_t)(uintptr_t)entry;
	uint64_t sp0 = (uint64_t)(uintptr_t)stack_top;
	uint64_t spsr = SPSR_EL1_EL0T_AARCH64_DAIF_UNMASKED;

	if (current_process && process_mm_root(current_process))
		paging_activate_address_space((uintptr_t)process_mm_root(current_process));

	/* musl expects the TLS base in TPIDR_EL0 before first user insn. */
	if (current_process)
		tls_set_base(process_tls_get(current_process));

	__asm__ volatile(
		"msr	sp_el0, %0\n"
		"msr	elr_el1, %1\n"
		"msr	spsr_el1, %2\n"
		"isb\n"
		"eret\n"
		:
		: "r"(sp0), "r"(elr), "r"(spsr)
		: "memory");
	__builtin_unreachable();
}

void switch_to_user_task(const task_t *task)
{
	process_t *p;

	if (!task)
		panic("switch_to_user_task: null task");

	p = task_to_process(task);
	if (!p)
		panic("switch_to_user_task: orphan task");

	switch_to_user((arch_addr_t)(uintptr_t)task_get_ip(task),
		       (arch_addr_t)(uintptr_t)task_get_sp(task));
}
