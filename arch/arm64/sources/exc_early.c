/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: exc_early.c
 * Description: VBAR, SVC ABI, IRQ (timer), EL0 drop, PSCI HVC (product + freestanding).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "exc_early.h"
#include "pl011.h"
#include "irq_backend.h"
#include <ir0/irq.h>
#include "timer.h"

#include <config.h>

#include <ir0/process.h>
#include <ir0/percpu.h>
#include <ir0/sched.h>

#include <stdint.h>
#include <ir0/boot_log.h>
#include <ir0/errno.h>
#include <ir0/clock.h>
#include <ir0/oops.h>
#include <ir0/signals.h>
#include <ir0/page_fault.h>
#include <ir0/tls.h>
#include <asm/exc_frame.h>


/* ESR_EL1: EC 0x15 = SVC instruction from AArch64. */
#define ESR_EC_SHIFT 26
#define ESR_EC_MASK  0x3FUL
#define ESR_EC_SVC64 0x15UL
#define ESR_EC_FPASIMD 0x07UL /* trapped FP/SIMD access */
#define ESR_EC_IABT_LOWER 0x20UL
#define ESR_EC_PC_ALIGN 0x22UL
#define ESR_EC_SP_ALIGN 0x26UL
#define ESR_EC_DABT_LOWER 0x24UL
#define ESR_EC_IABT_CUR 0x21UL
#define ESR_EC_DABT_CUR 0x25UL
#define ESR_EC_BRK64 0x3CUL

/* PSCI 0.2 (SMC Calling Convention) — QEMU virt uses HVC conduit at EL1. */
#define PSCI_0_2_FN_SYSTEM_OFF   0x84000008UL
#define PSCI_0_2_FN_SYSTEM_RESET 0x84000009UL

extern char ir0_el1_vectors[];

int64_t syscall_dispatch(uint64_t syscall_num, uint64_t arg1, uint64_t arg2,
			 uint64_t arg3, uint64_t arg4, uint64_t arg5,
			 uint64_t arg6);

static volatile int g_timer_irq_seen;
static int g_el0_svc_tagged;

extern process_t *current_process;

static void exc_put_hex64(uint64_t value)
{
	static const char digits[] = "0123456789abcdef";
	int shift;

	for (shift = 60; shift >= 0; shift -= 4)
		pl011_putc(digits[(value >> (unsigned)shift) & 0xfU]);
}

int arm64_timer_irq_seen(void)
{
	return g_timer_irq_seen;
}

static void psci_hvc(uint64_t fn)
{
	register uint64_t x0 __asm__("x0") = fn;

	__asm__ volatile("hvc #0" : "+r"(x0) :: "memory", "x1", "x2", "x3");
}

void arm64_psci_system_off(void)
{
	ir0_boot_smoke("ARM64_PSCI_OFF");
	psci_hvc(PSCI_0_2_FN_SYSTEM_OFF);
	for (;;)
	{
		__asm__ volatile("wfi" ::: "memory");
	}
}

void arm64_psci_system_reset(void)
{
	psci_hvc(PSCI_0_2_FN_SYSTEM_RESET);
	for (;;)
	{
		__asm__ volatile("wfi" ::: "memory");
	}
}

int arm64_vbar_early_install(void)
{
	uint64_t vbar = (uint64_t)(uintptr_t)ir0_el1_vectors;

	if ((vbar & 0x7FFUL) != 0)
	{
		return -EINVAL;
	}

	__asm__ volatile("msr vbar_el1, %0" :: "r"(vbar) : "memory");
	__asm__ volatile("isb" ::: "memory");
	return 0;
}

void arm64_exc_trigger_svc(void)
{
	__asm__ volatile("svc #0" ::: "memory");
}

static void arm64_abort_to_common_mm(uint64_t esr, uint64_t *frame);

void arm64_exc_sync_el1(void)
{
	uint64_t esr;

	__asm__ volatile("mrs %0, esr_el1" : "=r"(esr));
	if (((esr >> ESR_EC_SHIFT) & ESR_EC_MASK) == ESR_EC_SVC64)
	{
		ir0_boot_smoke("ARM64_VBAR_OK");
	}
	else if (((esr >> ESR_EC_SHIFT) & ESR_EC_MASK) == ESR_EC_IABT_CUR ||
		 ((esr >> ESR_EC_SHIFT) & ESR_EC_MASK) == ESR_EC_DABT_CUR)
	{
		/* Kernel abort: uaccess SIGSEGV or panic, decided by common MM. */
		arm64_abort_to_common_mm(esr, NULL);
	}
	else
	{
		uint64_t elr;
		uint64_t far;

		__asm__ volatile("mrs %0, elr_el1" : "=r"(elr));
		__asm__ volatile("mrs %0, far_el1" : "=r"(far));
		pl011_puts("ARM64_EL1_ESR_");
		exc_put_hex64(esr);
		pl011_puts("_ELR_");
		exc_put_hex64(elr);
		pl011_puts("_FAR_");
		exc_put_hex64(far);
		pl011_putc('\n');
		panic("arm64: unhandled EL1 synchronous exception");
	}
}

void arm64_exc_irq_el1(void)
{
	uint32_t iar = arm64_irq_backend_ack();
	uint32_t irq = iar & 0x3ffU;

	cpu_local_irq_enter();

	arch_timer_oneshot_disarm();

	if (irq == ARM64_IRQ_PHYS_TIMER)
	{
		if (!g_timer_irq_seen)
		{
			g_timer_irq_seen = 1;
			ir0_boot_smoke("ARM64_TIMER_IRQ_OK");
		}

		if (clock_is_ready())
		{
			clock_note_irq();
			clock_tick();
			arch_timer_periodic_arm();
		}
	}
	else
		(void)irq_dispatch(irq);

	if (irq < 1020U)
	{
		arm64_irq_backend_eoi(iar);
	}

	(void)cpu_local_irq_leave();
}

/*
 * Instruction/data abort: decode ESR/FAR and run the common demand-paging,
 * COW and SIGSEGV policy, as x86 does from its #PF vector.
 */
static void arm64_abort_to_common_mm(uint64_t esr, uint64_t *frame)
{
	struct page_fault_info info;

	if (page_fault_decode(&info, esr, frame) != 0)
		panic("arm64: abort decode failed");
	mm_page_fault_handle(&info, frame);
}

/*
 * EL0 synchronous exception that is neither a syscall nor an abort:
 * terminate the task with the Linux signal for that class
 * (arch/arm64/kernel/traps.c). Returning would replay the instruction
 * forever.
 */
static void arm64_el0_fault_kill(uint64_t esr)
{
	process_t *p = current_process;
	uint64_t ec = (esr >> ESR_EC_SHIFT) & ESR_EC_MASK;
	uint64_t far;
	uint64_t elr;
	uint64_t sp_el0;
	int sig;

	__asm__ volatile("mrs %0, far_el1" : "=r"(far));
	__asm__ volatile("mrs %0, elr_el1" : "=r"(elr));
	__asm__ volatile("mrs %0, sp_el0" : "=r"(sp_el0));
	/* Vector 6 = invalid-opcode class in the shared fault log. */
	ir0_log_user_fault_frame(6U, far, esr, elr, 0, sp_el0, 0, 0, 0,
				 p ? (unsigned)p->task.pid : 0U,
				 p ? p->comm : "(none)");

	if (ec == ESR_EC_BRK64)
		sig = SIGTRAP;
	else if (ec == ESR_EC_PC_ALIGN || ec == ESR_EC_SP_ALIGN)
		sig = SIGBUS;
	else
		sig = SIGILL;

	if (!p || p->mode != USER_MODE)
		panic("arm64: EL0 exception without a user task");
	signal_exit_current(sig);
}

void arm64_exc_sync_lower(uint64_t *frame)
{
	uint64_t esr;
	int64_t ret;

	__asm__ volatile("mrs %0, esr_el1" : "=r"(esr));
	if (((esr >> ESR_EC_SHIFT) & ESR_EC_MASK) != ESR_EC_SVC64)
	{
		uint64_t ec = (esr >> ESR_EC_SHIFT) & ESR_EC_MASK;

		if (ec == ESR_EC_FPASIMD)
		{
			uint64_t cpacr;

			/* Lazy enable: retry the trapping instruction. */
			__asm__ volatile("mrs %0, cpacr_el1" : "=r"(cpacr));
			cpacr |= (3UL << 20);
			__asm__ volatile("msr cpacr_el1, %0" :: "r"(cpacr)
					 : "memory");
			__asm__ volatile("isb" ::: "memory");
			return;
		}
		if (ec == ESR_EC_IABT_LOWER)
			ir0_boot_smoke("ARM64_EL0_INSN_ABORT");
		else if (ec == ESR_EC_DABT_LOWER)
			ir0_boot_smoke("ARM64_EL0_DATA_ABORT");
		else if (ec == ESR_EC_BRK64)
			ir0_boot_smoke("ARM64_EL0_BREAKPOINT");
		else
		{
			ir0_boot_smoke("ARM64_EL0_SYNC_OTHER");
			pl011_puts("ARM64_EL0_ESR_");
			exc_put_hex64(esr);
			pl011_puts("\n");
		}
		if (ec == ESR_EC_IABT_LOWER || ec == ESR_EC_DABT_LOWER)
			arm64_abort_to_common_mm(esr, frame);
		else
			arm64_el0_fault_kill(esr);
		return;
	}

	if (!g_el0_svc_tagged)
	{
		g_el0_svc_tagged = 1;
		ir0_boot_smoke("ARM64_EL0_SVC_OK");
#if defined(IR0_ARM64_PRODUCT_KERNEL)
		ir0_boot_smoke("ARM64_PRODUCT_EL0_SYSCALL_OK");
#endif
	}

	/*
	 * frame: x0@0 … x30 in the 256-byte stub slot (vectors.S);
	 * x8 = syscall number. Return value in frame[0].
	 */
	/*
	 * Same entry contract as x86 syscall_64.asm: snapshot the user frame
	 * (fork, clone, sigreturn and restart read p->syscall_frame), dispatch,
	 * then reload the thread pointer of whichever task returns to EL0.
	 */
	process_capture_syscall_frame_at_entry(frame, frame[ARM64_EXC_IDX_ELR]);
	ret = syscall_dispatch(frame[8], frame[0], frame[1], frame[2],
			       frame[3], frame[4], frame[5]);
	frame[0] = (uint64_t)ret;
	tls_restore_current();
}
