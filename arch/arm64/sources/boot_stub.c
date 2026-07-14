/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: boot_stub.c
 * Description: ARM64 QEMU virt early boot — PL011, MMU, GIC, F7h switch, F7c EL0.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "mmu_early.h"
#include "exc_early.h"
#include "slice_hello.h"
#include "pl011.h"
#include "timer.h"
#include "gic_v2.h"
#include "switch_early.h"
#include "process_early.h"
#include "elf_load_early.h"
#include "rr_early.h"
#include "virtio_blk_early.h"
#include "virtio_net_early.h"

#include <arch/common/arch_portable.h>
#include <ir0/virtio_mmio.h>
#include <stdint.h>

void __attribute__((weak)) arm64_all_objs_mark(void)
{
}

#define BOOT_STACK_SIZE 4096
#define VIRT_GIC_DIST   0x08000000UL
#define USER_PAGE_SIZE  4096
/* Dedicated DRAM page (32 MiB into RAM) — avoids L3-split of kernel text 2 MiB. */
#define ARM64_EL0_USER_PAGE_PA 0x42000000UL

/* Global: referenced from _start asm (must be linker-visible). */
uint8_t boot_stack[BOOT_STACK_SIZE] __attribute__((aligned(16)));

static void fill_user_page(void)
{
	static const char msg[] = "ARM64_WRITE_OK\n";
	volatile uint8_t *p = (volatile uint8_t *)(uintptr_t)ARM64_EL0_USER_PAGE_PA;
	volatile int64_t *ts;
	unsigned i;

	for (i = 0; msg[i]; i++)
	{
		p[i] = (uint8_t)msg[i];
	}
	for (; i < USER_PAGE_SIZE; i++)
	{
		p[i] = 0;
	}

	/* timespec64 for nanosleep smoke at +0x40 (sec=0, nsec=1ms). */
	ts = (volatile int64_t *)(p + 0x40);
	ts[0] = 0;
	ts[1] = 1000000;
}


static void arm64_irq_oneshot_demo(void)
{
	unsigned spins;

	if (arm64_gic_v2_enable(ARM64_GIC_PPI_PHYS_TIMER) != 0)
	{
		pl011_puts("ARM64_GIC_FAIL\n");
		return;
	}
	pl011_puts("ARM64_GIC_OK\n");

	{
		unsigned long irqf = arch_irq_save();

		arch_timer_oneshot_arm(10000U);
		/* Unmask IRQ (DAIF.I = bit 7) while keeping other DAIF bits. */
		arch_irq_restore(irqf & ~(1UL << 7));

		for (spins = 0; spins < 2000000U && !arm64_timer_irq_seen(); spins++)
		{
			__asm__ volatile("wfi" ::: "memory");
		}

		(void)arch_irq_save();
		arch_timer_oneshot_disarm();
	}

	if (!arm64_timer_irq_seen())
	{
		pl011_puts("ARM64_TIMER_IRQ_FAIL\n");
	}
}

void boot_main(void)
{
	pl011_init();
	pl011_puts("ARM64_BOOT_OK\n");
	arm64_all_objs_mark();

	if (arm64_mmu_early_enable() != 0)
	{
		pl011_puts("ARM64_MMU_FAIL\n");
		goto idle;
	}
	pl011_puts("ARM64_MMU_OK\n");

	pl011_init();
	pl011_puts("ARM64_PL011_OK\n");

	if (arm64_mmu_early_verify() == 0)
	{
		pl011_puts("ARM64_PAGING_OK\n");
	}
	else
	{
		pl011_puts("ARM64_PAGING_FAIL\n");
	}

	if (arm64_mmu_map_device_block(VIRT_GIC_DIST) == 0)
	{
		pl011_puts("ARM64_GIC_MAP_OK\n");
	}
	else
	{
		pl011_puts("ARM64_GIC_MAP_FAIL\n");
	}

	if (arm64_mmu_map_user_page(ARM64_EL0_USER_PAGE_PA) == 0)
	{
		fill_user_page();
		pl011_puts("ARM64_EL0_PAGE_OK\n");
	}
	else
	{
		pl011_puts("ARM64_EL0_PAGE_FAIL\n");
	}

	arm64_slice_after_mmu();

	arch_timer_init();
	if (arch_timer_smoke_ok() == 0)
	{
		pl011_puts("ARM64_TIMER_OK\n");
	}
	else
	{
		pl011_puts("ARM64_TIMER_FAIL\n");
	}

	if (arm64_vbar_early_install() != 0)
	{
		pl011_puts("ARM64_VBAR_FAIL\n");
		goto idle;
	}

	arm64_irq_oneshot_demo();

	arm64_exc_trigger_svc();
	pl011_puts("ARM64_SVC_RET_OK\n");

	arm64_switch_early_smoke();

	if (arm64_mmu_ttbr_dual_smoke() == 0)
	{
		pl011_puts("ARM64_TTBR_B_OK\n");
		pl011_puts("ARM64_TTBR_SWITCH_OK\n");
	}
	else
	{
		pl011_puts("ARM64_TTBR_SWITCH_FAIL\n");
	}

	(void)arm64_process_ttbr_smoke();
	(void)arm64_fork_exec_smoke();
	(void)arm64_process_t_switch_smoke();
	(void)arm64_rr_sched_smoke();

	/* Virtio-mmio (QEMU -device virtio-*-device); tags fail soft if absent. */
	if (arm64_virtio_mmio_probe() == 0)
	{
		(void)arm64_virtio_blk_smoke();
		(void)arm64_virtio_net_smoke();
	}

	/* Musl hello EL0 (noreturn on success via eret → after_musl → enter_el0). */
	if (arm64_musl_hello_el0() != 0)
		arm64_enter_el0();

idle:
	for (;;)
	{
		__asm__ volatile("wfi" ::: "memory");
	}
}

void __attribute__((section(".text.boot"), noreturn)) _start(void)
{
	__asm__ volatile(
		"adrp	x0, boot_stack\n"
		"add	x0, x0, :lo12:boot_stack\n"
		"add	sp, x0, %[sz]\n"
		"b	boot_main\n"
		:
		: [sz] "i"(BOOT_STACK_SIZE)
		: "x0", "memory");
	__builtin_unreachable();
}
