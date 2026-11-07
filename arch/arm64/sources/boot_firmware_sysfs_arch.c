/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: boot_firmware_sysfs_arch.c
 * Description: ARM64 firmware/DTB snapshot for /sys/firmware/ir0/*.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/arm64_board.h>
#include <ir0/boot_firmware_sysfs.h>

#include <string.h>

static const char *irq_model_name(enum arm64_irq_controller_model m)
{
	switch (m)
	{
	case ARM64_IRQ_CONTROLLER_GIC_V2:
		return "gic_v2";
	case ARM64_IRQ_CONTROLLER_GIC_V3:
		return "gic_v3";
	case ARM64_IRQ_CONTROLLER_BCM2836_LOCAL:
		return "bcm2836_local";
	default:
		return "unknown";
	}
}

static const char *hv_kind_name(enum arm64_hypervisor_kind k)
{
	switch (k)
	{
	case ARM64_HV_QEMU:
		return "qemu";
	case ARM64_HV_KVM:
		return "kvm";
	case ARM64_HV_XEN:
		return "xen";
	case ARM64_HV_HYPERV:
		return "hyperv";
	case ARM64_HV_VMWARE:
		return "vmware";
	case ARM64_HV_OTHER:
		return "other";
	case ARM64_HV_NONE:
	default:
		return "none";
	}
}

static const char *psci_name(enum arm64_psci_conduit c)
{
	switch (c)
	{
	case ARM64_PSCI_CONDUIT_HVC:
		return "hvc";
	case ARM64_PSCI_CONDUIT_SMC:
		return "smc";
	default:
		return "unknown";
	}
}

void ir0_boot_firmware_sysfs_fill_from_arch(struct ir0_boot_firmware_sysfs_snapshot *snap)
{
	const struct arm64_board_boot_info *b;
	uint32_t i;

	if (!snap)
		return;

	b = arm64_board_boot_info();
	if (!b)
	{
		snprintf(snap->protocol, sizeof(snap->protocol), "unknown");
		return;
	}

	if (b->fdt_valid)
		snprintf(snap->protocol, sizeof(snap->protocol), "fdt");
	else
		snprintf(snap->protocol, sizeof(snap->protocol), "dtb_invalid");

	snprintf(snap->bootloader, sizeof(snap->bootloader), "firmware-dtb-handoff");
	snprintf(snap->cmdline, sizeof(snap->cmdline), "%s", b->bootargs);

	if (b->framebuffer.size != 0U)
	{
		snprintf(snap->framebuffer, sizeof(snap->framebuffer),
			 "addr=0x%llx size=0x%llx",
			 (unsigned long long)b->framebuffer.base,
			 (unsigned long long)b->framebuffer.size);
	}
	else
	{
		snprintf(snap->framebuffer, sizeof(snap->framebuffer), "unavailable");
	}

	snprintf(snap->boot_detail, sizeof(snap->boot_detail),
		 "fdt_valid=%d\n"
		 "fdt_pa=0x%llx fdt_size=%u fdt_magic=0x%08x\n"
		 "cpu_count=%u\n"
		 "irq_controller=%s irq_ranges=%u\n"
		 "psci_conduit=%s architected_timer=%d rp1_present=%d\n"
		 "hypervisor=%s vendor=%s\n"
		 "memory_banks=%u reserved=%u usable=%u\n",
		 b->fdt_valid,
		 (unsigned long long)b->fdt_pa, (unsigned)b->fdt_size,
		 (unsigned)b->fdt_magic,
		 (unsigned)b->cpu_count,
		 irq_model_name(b->irq_controller),
		 (unsigned)b->irq_range_count,
		 psci_name(b->psci_conduit), b->architected_timer, b->rp1_present,
		 hv_kind_name(b->hypervisor_kind), b->hypervisor_vendor,
		 (unsigned)b->memory_range_count,
		 (unsigned)b->reserved_range_count,
		 (unsigned)b->usable_range_count);

	/* Append first memory bank lines (bounded) for firmware RAM layout. */
	for (i = 0; i < b->memory_range_count && i < 4U; i++)
	{
		size_t cur = strlen(snap->boot_detail);
		int n;

		if (cur + 80 >= sizeof(snap->boot_detail))
			break;
		n = snprintf(snap->boot_detail + cur,
			     sizeof(snap->boot_detail) - cur,
			     "memory[%u] base=0x%llx size=0x%llx\n",
			     (unsigned)i, (unsigned long long)b->memory[i].base,
			     (unsigned long long)b->memory[i].size);
		if (n < 0)
			break;
	}
}
