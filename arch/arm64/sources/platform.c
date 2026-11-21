/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: platform.c
 * Description: ARM64 platform ops — virt PSCI + RPi stub; board selects via arm64_board.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <arch/common/arch_portable.h>
#include <asm/processor.h>
#include <arm64_board.h>
#include <ir0/platform_ops.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ARM DEN 0028 fast SMC32, vendor-hypervisor owner, Call UID query. */
#define ARM64_SMCCC_VENDOR_HYP_UID 0x8600ff01u

/* KVM UID 28b46fb6-2ec5-11e9-a9ca-4b564d003a74 as four SMCCC words. */
#define ARM64_SMCCC_KVM_UID_0 0xb66fb428u
#define ARM64_SMCCC_KVM_UID_1 0xe911c52eu
#define ARM64_SMCCC_KVM_UID_2 0x564bcaa9u
#define ARM64_SMCCC_KVM_UID_3 0x743a004du

uint32_t get_cpu_id(void)
{
	return 0;
}

uint32_t get_cpu_count(void)
{
	return 1;
}

int get_cpu_vendor(char *vendor_buf)
{
	if (!vendor_buf)
		return -1;
	strncpy(vendor_buf, "Unknown", 8);
	return -1;
}

int get_cpu_signature(uint32_t *family, uint32_t *model, uint32_t *stepping)
{
	if (family)
		*family = 0;
	if (model)
		*model = 0;
	if (stepping)
		*stepping = 0;
	return -1;
}

int get_cpuid_max_leaf(uint32_t *max_leaf)
{
	if (max_leaf)
		*max_leaf = 0;
	return -1;
}

int get_cpu_brand_string(char *buf, size_t size)
{
	(void)buf;
	(void)size;
	return -1;
}

int get_cpu_feature_bits(uint32_t *out_edx, uint32_t *out_ecx)
{
	if (out_edx)
		*out_edx = 0;
	if (out_ecx)
		*out_ecx = 0;
	return -1;
}

uint32_t get_cpu_clflush_size(void)
{
	return 0;
}

static int arm64_smccc_not_supported(uint64_t r0)
{
	return r0 == ~(uint64_t)0 || (uint32_t)r0 == 0xffffffffu;
}

static int arm64_smccc_uid(uint64_t *r0, uint64_t *r1, uint64_t *r2,
			   uint64_t *r3)
{
	register uint64_t x0 __asm__("x0") = ARM64_SMCCC_VENDOR_HYP_UID;
	register uint64_t x1 __asm__("x1") = 0;
	register uint64_t x2 __asm__("x2") = 0;
	register uint64_t x3 __asm__("x3") = 0;
	enum arm64_psci_conduit conduit;

	conduit = arm64_board_boot_info()->psci_conduit;
	if (conduit == ARM64_PSCI_CONDUIT_HVC)
	{
		__asm__ volatile("hvc #0"
				 : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
				 :
				 : "memory", "x4", "x5", "x6", "x7", "x8",
				   "x9", "x10", "x11", "x12", "x13", "x14",
				   "x15", "x16", "x17");
	}
	else if (conduit == ARM64_PSCI_CONDUIT_SMC)
	{
		__asm__ volatile("smc #0"
				 : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
				 :
				 : "memory", "x4", "x5", "x6", "x7", "x8",
				   "x9", "x10", "x11", "x12", "x13", "x14",
				   "x15", "x16", "x17");
	}
	else
	{
		return -1;
	}
	*r0 = x0;
	*r1 = x1;
	*r2 = x2;
	*r3 = x3;
	return 0;
}

int hypervisor_kind(enum ir0_hv_kind *kind)
{
	static int cached;
	static int ran;
	static enum ir0_hv_kind found;
	const struct arm64_board_boot_info *info;
	uint64_t r0;
	uint64_t r1;
	uint64_t r2;
	uint64_t r3;
	int probed = 0;

	if (!kind)
		return -1;
	if (cached)
	{
		if (!ran)
			return -1;
		*kind = found;
		return 0;
	}

	info = arm64_board_boot_info();
	if (info == NULL)
	{
		cached = 1;
		ran = 0;
		return -1;
	}
	if (info->fdt_valid)
		probed = 1;
	found = IR0_HV_NONE;
	if (arm64_smccc_uid(&r0, &r1, &r2, &r3) == 0)
	{
		probed = 1;
		if (!arm64_smccc_not_supported(r0))
		{
			if ((uint32_t)r0 == ARM64_SMCCC_KVM_UID_0 &&
			    (uint32_t)r1 == ARM64_SMCCC_KVM_UID_1 &&
			    (uint32_t)r2 == ARM64_SMCCC_KVM_UID_2 &&
			    (uint32_t)r3 == ARM64_SMCCC_KVM_UID_3)
				found = IR0_HV_KVM;
			else
				found = IR0_HV_OTHER;
		}
	}
	if (found != IR0_HV_KVM && info->hypervisor_kind == ARM64_HV_XEN)
		found = IR0_HV_XEN;
	if (!probed)
	{
		cached = 1;
		ran = 0;
		return -1;
	}
	ran = 1;
	cached = 1;
	*kind = found;
	return 0;
}

int hypervisor_present(void)
{
	enum ir0_hv_kind kind;

	if (hypervisor_kind(&kind) < 0)
		return 0;
	return kind != IR0_HV_NONE;
}

int hypervisor_vendor(char *buf, size_t n)
{
	/*
	 * This ISA has no CPUID vendor leaf. An empty answer is not a
	 * synthesized x86 vendor string.
	 */
	if (buf && n)
		buf[0] = '\0';
	return -1;
}

static void __attribute__((noreturn)) arm64_halt_loop(void)
{
	disable_interrupts();
	for (;;)
		cpu_wait();
}

static int arm64_psci_call(uint64_t fn,
			   enum arm64_psci_conduit fallback_conduit)
{
	register uint64_t x0 __asm__("x0") = fn;
	enum arm64_psci_conduit conduit = arm64_board_boot_info()->psci_conduit;

	if (conduit == ARM64_PSCI_CONDUIT_UNKNOWN)
		conduit = fallback_conduit;
	if (conduit == ARM64_PSCI_CONDUIT_HVC)
		__asm__ volatile("hvc #0" : "+r"(x0) :: "memory", "x1", "x2", "x3");
	else if (conduit == ARM64_PSCI_CONDUIT_SMC)
		__asm__ volatile("smc #0" : "+r"(x0) :: "memory", "x1", "x2", "x3");
	else
		return -1;
	return 0;
}

static void arm64_virt_halt(void)
{
	arm64_halt_loop();
}

static void arm64_virt_reboot(void)
{
	/* PSCI 0.2 SYSTEM_RESET — QEMU virt HVC conduit. */
	(void)arm64_psci_call(0x84000009UL, ARM64_PSCI_CONDUIT_HVC);
	arm64_halt_loop();
}

static void arm64_virt_poweroff(void)
{
	/* PSCI 0.2 SYSTEM_OFF — QEMU virt HVC conduit. */
	(void)arm64_psci_call(0x84000008UL, ARM64_PSCI_CONDUIT_HVC);
	arm64_halt_loop();
}

/* QEMU virt (default) — selected via arm64_board_get(). */
const struct ir0_platform_ops arm64_virt_platform_ops = {
	.halt = arm64_virt_halt,
	.reboot = arm64_virt_reboot,
	.poweroff = arm64_virt_poweroff,
};

/*
 * Raspberry Pi stub — same WFI halt until PSCI/mailbox power is wired.
 * Selected via arm64_board (rpi4 / rpi5) or ir0_platform_ops_set().
 */
static void arm64_rpi_halt(void)
{
	arm64_halt_loop();
}

static void arm64_rpi_reboot(void)
{
	(void)arm64_psci_call(0x84000009UL, ARM64_PSCI_CONDUIT_UNKNOWN);
	arm64_halt_loop();
}

static void arm64_rpi_poweroff(void)
{
	(void)arm64_psci_call(0x84000008UL, ARM64_PSCI_CONDUIT_UNKNOWN);
	arm64_halt_loop();
}

const struct ir0_platform_ops arm64_rpi_platform_ops = {
	.halt = arm64_rpi_halt,
	.reboot = arm64_rpi_reboot,
	.poweroff = arm64_rpi_poweroff,
};

static const struct ir0_platform_ops *g_platform_ops = &arm64_virt_platform_ops;

const struct ir0_platform_ops *ir0_platform_ops_get(void)
{
	return g_platform_ops;
}

void ir0_platform_ops_set(const struct ir0_platform_ops *ops)
{
	if (ops)
		g_platform_ops = ops;
}

