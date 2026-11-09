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
#include <ir0/boot_log.h>
#include <ir0/task.h>

#include <stdint.h>

/*
 * Saved Program Status at exception return to AArch64 EL0 (EL0t).
 * Bits [9:6] DAIF: clear so IRQ/FIQ are not masked at EL0 (device IRQs after eret).
 * M[3:0]=0: EL0t (AArch64). Matches the early exception path (SPSR_MODE_EL0T).
 */
#define SPSR_EL1_EL0T_AARCH64_DAIF_UNMASKED 0x0UL

/* CPACR_EL1.FPEN=0b11: no trap on FP/SIMD at EL0/EL1 (musl crt uses NEON). */
static void arm64_enable_fp_simd(void)
{
	uint64_t cpacr;

	__asm__ volatile("mrs %0, cpacr_el1" : "=r"(cpacr));
	cpacr |= (3UL << 20);
	__asm__ volatile("msr cpacr_el1, %0" :: "r"(cpacr) : "memory");
	__asm__ volatile("isb" ::: "memory");
}

/*
 * Return to EL0 (Linux kernel_exit). Programs the return state with every
 * exception masked (a trap between the msr writes and eret would overwrite
 * ELR/SPSR), points SP_EL1 at the top of this task's kernel stack so the next
 * trap lands on its own stack (x86 loads TSS.RSP0), and loads x0..x30 from
 * @gprs. Never returns; the caller's C frame is abandoned.
 */
static void __attribute__((noreturn))
arm64_eret_to_user(uint64_t elr, uint64_t sp0, const uint64_t *gprs)
{
	uint64_t kstack;

	if (!current_process || !current_process->kstack_top)
		panic("arm64: EL0 entry without a kernel stack");
	kstack = current_process->kstack_top & ~0xfUL;

	if (process_mm_root(current_process))
		paging_activate_address_space((uintptr_t)process_mm_root(current_process));
	/* musl expects the TLS base in TPIDR_EL0 before the first user insn. */
	tls_set_base(process_tls_get(current_process));
	arm64_enable_fp_simd();
	ir0_boot_smoke("ARM64_USER_ERET");

	/* Bind operands only now: the calls above clobber x9..x13. */
	register uint64_t r_sp0 __asm__("x9") = sp0;
	register uint64_t r_elr __asm__("x10") = elr;
	register uint64_t r_spsr __asm__("x11") = SPSR_EL1_EL0T_AARCH64_DAIF_UNMASKED;
	register uint64_t r_kstack __asm__("x12") = kstack;
	register const uint64_t *r_gprs __asm__("x13") = gprs;

	__asm__ volatile(
		"msr	daifset, #0xf\n"
		"msr	sp_el0, x9\n"
		"msr	elr_el1, x10\n"
		"msr	spsr_el1, x11\n"
		"mov	sp, x12\n"
		"mov	x30, x13\n"
		"ldp	x0, x1, [x30, #0]\n"
		"ldp	x2, x3, [x30, #16]\n"
		"ldp	x4, x5, [x30, #32]\n"
		"ldp	x6, x7, [x30, #48]\n"
		"ldp	x8, x9, [x30, #64]\n"
		"ldp	x10, x11, [x30, #80]\n"
		"ldp	x12, x13, [x30, #96]\n"
		"ldp	x14, x15, [x30, #112]\n"
		"ldp	x16, x17, [x30, #128]\n"
		"ldp	x18, x19, [x30, #144]\n"
		"ldp	x20, x21, [x30, #160]\n"
		"ldp	x22, x23, [x30, #176]\n"
		"ldp	x24, x25, [x30, #192]\n"
		"ldp	x26, x27, [x30, #208]\n"
		"ldp	x28, x29, [x30, #224]\n"
		"ldr	x30, [x30, #240]\n"
		"isb\n"
		"eret\n"
		:
		: "r"(r_sp0), "r"(r_elr), "r"(r_spsr), "r"(r_kstack), "r"(r_gprs)
		: "memory");
	__builtin_unreachable();
}

/* New image (execve, init): Linux starts EL0 with every GPR cleared. */
void switch_to_user(arch_addr_t entry, arch_addr_t stack_top)
{
	static const uint64_t zero_gprs[31];

	arm64_eret_to_user((uint64_t)(uintptr_t)entry,
			   (uint64_t)(uintptr_t)stack_top, zero_gprs);
}

/* Resume a task's saved user state (fork child, signal handler entry). */
void switch_to_user_task(const task_t *task)
{
	const process_t *p;

	if (!task)
		panic("switch_to_user_task: null task");

	p = task_to_process_const(task);
	if (!p)
		panic("switch_to_user_task: orphan task");

	/* x0..x30 are the leading fields of arch_task_context_t. */
	arm64_eret_to_user(task_get_ip(task), task_get_sp(task), &task->arch.x0);
}
