/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: boot_common.c
 * Description: ARM64 freestanding bring-up (MMU, IRQ, virtio) — shared by all boards.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "boot_common.h"
#include "mmu.h"
#include "exc_early.h"
#include "pl011.h"
#include "timer.h"
#include "gic_v2.h"
#include "irq_backend.h"
#include "virtio_blk_early.h"

#include <arch/common/arch_portable.h>
#include <arm64_board.h>
#include <asm/boot.h>
#include <ir0/boot_memory.h>
#include <ir0/boot_log.h>
#include <ir0/percpu.h>
#include <ir0/virtio_mmio.h>
#include <config.h>

#include <stdint.h>

#define BOOT_STACK_SIZE ARM64_BOOT_STACK_SIZE
uint8_t boot_stack[BOOT_STACK_SIZE] __attribute__((aligned(16)));

#define VIRT_GIC_DIST 0x08000000UL
#define VIRT_GIC_CPU  0x08010000UL
#define VIRT_GIC_SIZE 0x00010000UL

#if DEBUG_BOOT
static void arm64_irq_oneshot_demo(void)
{
	const struct arm64_board_boot_info *boot_info = arm64_board_boot_info();
	unsigned spins;

	if (arm64_irq_backend_enable(ARM64_IRQ_PHYS_TIMER) != 0)
	{
		ir0_boot_smoke("ARM64_IRQ_BACKEND_FAIL");
		return;
	}
	ir0_boot_smoke("ARM64_IRQ_BACKEND_OK");
	if (boot_info &&
	    boot_info->irq_controller == ARM64_IRQ_CONTROLLER_BCM2836_LOCAL)
		ir0_boot_smoke("ARM64_BCM2836_IRQ_OK");
	else
		ir0_boot_smoke("ARM64_GIC_OK");

	{
		unsigned long irqf = irq_save();

		arch_timer_oneshot_arm(10000U);
		irq_restore(irqf & ~(1UL << 7));

		for (spins = 0; spins < 2000000U && !arm64_timer_irq_seen(); spins++)
		{
			__asm__ volatile("wfi" ::: "memory");
		}

		(void)irq_save();
		arch_timer_oneshot_disarm();
	}

	if (!arm64_timer_irq_seen())
	{
		ir0_boot_smoke("ARM64_TIMER_IRQ_FAIL");
	}
	else if (cpu_local()->irq_depth == 0 && cpu_local()->interrupts != 0)
	{
		ir0_boot_smoke("ARM64_IRQ_ACCOUNTING_OK");
	}
	else
	{
		ir0_boot_smoke("ARM64_IRQ_ACCOUNTING_FAIL");
	}
}
#endif

static int arm64_irq_select_and_map(const struct arm64_board_boot_info *boot_info)
{
	uint32_t i;

	if (!boot_info || boot_info->irq_controller == ARM64_IRQ_CONTROLLER_UNKNOWN ||
	    boot_info->irq_range_count == 0U)
		return -1;
	for (i = 0; i < boot_info->irq_range_count; i++)
	{
		if (arm64_mmu_map_device_range(boot_info->irq_mmio[i].base,
					       boot_info->irq_mmio[i].size) != 0)
			return -1;
	}
	return arm64_irq_backend_select(boot_info->irq_controller,
					boot_info->irq_mmio,
					boot_info->irq_range_count);
}

static void arm64_board_capture_and_heap(uintptr_t firmware_handle)
{
	extern char _start[];
	extern char _end[];

	(void)arm64_board_capture_boot_info(firmware_handle);
	(void)arm64_board_prepare_memory((uintptr_t)_start,
					 (size_t)(_end - _start));
	arm64_board_apply_platform();
}

void arm64_platform_setup_for_portable_boot(uintptr_t firmware_handle)
{
	const struct arm64_board_boot_info *boot_info;

	arm64_board_capture_and_heap(firmware_handle);
	/*
	 * Snapshot firmware RAM while the MMU is still off. After TTBR0 is
	 * loaded, boot_info BSS must not be the only copy of the inventory.
	 */
	(void)boot_memory_snapshot_init(firmware_handle);

	pl011_init();

	/*
	 * Banner-first: ir0_boot_serial_ready() must be the first framed line
	 * (IR0 kernel <version>), then board/ARCH detail.
	 */
	ir0_boot_serial_ready();
	arm64_board_log_arch();
	ir0_boot_log_platform_hypervisor();
	ir0_boot_smoke("ARM64_BOOT_OK");

	if (arm64_mmu_enable() != 0)
	{
		ir0_boot_smoke("ARM64_MMU_FAIL");
		for (;;)
			__asm__ volatile("wfi" ::: "memory");
	}
#if DEBUG_BOOT
	ir0_boot_smoke("ARM64_MMU_OK");
	pl011_init();
	ir0_boot_smoke("ARM64_PL011_OK");

	if (arm64_mmu_verify() == 0)
		ir0_boot_smoke("ARM64_PAGING_OK");
	else
		ir0_boot_smoke("ARM64_PAGING_FAIL");
	if (arm64_mmu_map_device_range(0x1001ff000UL, 0x3000UL) == 0)
		ir0_boot_smoke("ARM64_MMIO_HIGH_OK");
	else
		ir0_boot_smoke("ARM64_MMIO_HIGH_FAIL");
#endif

	boot_info = arm64_board_boot_info();
	if ((!boot_info->fdt_valid &&
	     arm64_irq_backend_select(ARM64_IRQ_CONTROLLER_GIC_V2,
		(struct ir0_phys_range[2]){{VIRT_GIC_DIST, VIRT_GIC_SIZE},
					      {VIRT_GIC_CPU, VIRT_GIC_SIZE}}, 2U) == 0 &&
	     arm64_mmu_map_device_range(VIRT_GIC_DIST, VIRT_GIC_SIZE) == 0 &&
	     arm64_mmu_map_device_range(VIRT_GIC_CPU, VIRT_GIC_SIZE) == 0) ||
	    (boot_info->fdt_valid && arm64_irq_select_and_map(boot_info) == 0))
	{
#if DEBUG_BOOT
		ir0_boot_smoke("ARM64_IRQ_MAP_OK");
		if (boot_info->fdt_valid &&
		    boot_info->irq_controller == ARM64_IRQ_CONTROLLER_BCM2836_LOCAL)
			ir0_boot_smoke("ARM64_BCM2836_IRQ_MAP_OK");
		else
			ir0_boot_smoke("ARM64_GIC_MAP_OK");
#endif
	}
	else
	{
		ir0_boot_smoke("ARM64_IRQ_MAP_FAIL");
	}

	timer_init();
#if DEBUG_BOOT
	if (arch_timer_smoke_ok() == 0)
		ir0_boot_smoke("ARM64_TIMER_OK");
	else
		ir0_boot_smoke("ARM64_TIMER_FAIL");
#endif

	if (arm64_vbar_early_install() != 0)
	{
		ir0_boot_smoke("ARM64_VBAR_FAIL");
		for (;;)
			__asm__ volatile("wfi" ::: "memory");
	}

#if DEBUG_BOOT
	arm64_irq_oneshot_demo();

	arm64_exc_trigger_svc();
	ir0_boot_smoke("ARM64_SVC_RET_OK");
#endif
	mm_activate(arm64_mmu_kernel_root());

	{
		int virtio_probe = arm64_virtio_mmio_probe();

		if (virtio_probe == VIRTIO_MMIO_PROBE_OK)
		{
#if DEBUG_BOOT
			ir0_boot_smoke("ARM64_VIRTIO_MMIO_OK");
#endif
			if (arm64_virtio_mmio_find(VIRTIO_ID_BLOCK))
			{
				if (arm64_virtio_blk_init() == 0)
				{
#if DEBUG_BOOT
					ir0_boot_smoke("ARM64_VIRTIO_BLK_OK");
					if (arm64_virtio_blk_facade_probe() == 0)
						ir0_boot_smoke("ARM64_BLOCKDEV_FACADE_OK");
					else
						ir0_boot_smoke("ARM64_BLOCKDEV_FACADE_FAIL");
#endif
				}
				else
				{
					ir0_boot_smoke("ARM64_VIRTIO_BLK_FAIL");
				}
			}
		}
		else if (virtio_probe == VIRTIO_MMIO_PROBE_ABSENT)
		{
#if DEBUG_BOOT
			ir0_boot_smoke("ARM64_VIRTIO_MMIO_ABSENT");
#endif
		}
		else
		{
			ir0_boot_smoke("ARM64_VIRTIO_MMIO_FAIL");
		}
	}
}
