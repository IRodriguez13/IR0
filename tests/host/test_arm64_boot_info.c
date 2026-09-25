/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness.h"

#include <ir0/arm64_board.h>

#include <stdint.h>
#include <string.h>

static void put_be32(uint8_t *p, uint32_t value)
{
	p[0] = (uint8_t)(value >> 24);
	p[1] = (uint8_t)(value >> 16);
	p[2] = (uint8_t)(value >> 8);
	p[3] = (uint8_t)value;
}

static size_t append_be32(uint8_t *p, size_t off, uint32_t value)
{
	put_be32(p + off, value);
	return off + 4U;
}

static size_t append_name(uint8_t *p, size_t off, const char *name)
{
	size_t len = strlen(name) + 1U;

	memcpy(p + off, name, len);
	off += len;
	while ((off & 3U) != 0)
		p[off++] = 0;
	return off;
}

static size_t append_prop_u32(uint8_t *p, size_t off, uint32_t nameoff,
			      uint32_t value)
{
	off = append_be32(p, off, 3U);
	off = append_be32(p, off, 4U);
	off = append_be32(p, off, nameoff);
	return append_be32(p, off, value);
}

static size_t append_prop_bytes(uint8_t *p, size_t off, uint32_t nameoff,
				const void *value, uint32_t len)
{
	off = append_be32(p, off, 3U);
	off = append_be32(p, off, len);
	off = append_be32(p, off, nameoff);
	memcpy(p + off, value, len);
	off += len;
	while ((off & 3U) != 0U)
		p[off++] = 0;
	return off;
}

static size_t build_platform_fdt(uint8_t *fdt, size_t capacity)
{
	static const char strings[] =
		"#address-cells\0#size-cells\0reg\0compatible\0method\0";
	static const char gic_compat[] = "arm,gic-400\0arm,cortex-a15-gic";
	static const char timer_compat[] = "arm,armv8-timer";
	static const char psci_compat[] = "arm,psci-1.0";
	static const char psci_method[] = "smc";
	static const char rp1_compat[] = "raspberrypi,rp1-clocks";
	const size_t struct_off = 72U;
	size_t off = struct_off;
	size_t strings_off;

	memset(fdt, 0, capacity);
	/* One firmware reservation followed by the mandatory zero terminator. */
	put_be32(fdt + 40, 0U);
	put_be32(fdt + 44, 0x41000000U);
	put_be32(fdt + 48, 0U);
	put_be32(fdt + 52, 0x1000U);
	off = append_be32(fdt, off, 1U);
	off = append_name(fdt, off, "");
	off = append_prop_u32(fdt, off, 0U, 2U);
	off = append_prop_u32(fdt, off, 15U, 2U);
	off = append_be32(fdt, off, 1U);
	off = append_name(fdt, off, "memory@40000000");
	off = append_be32(fdt, off, 3U);
	off = append_be32(fdt, off, 16U);
	off = append_be32(fdt, off, 27U);
	off = append_be32(fdt, off, 0U);
	off = append_be32(fdt, off, 0x40000000U);
	off = append_be32(fdt, off, 0U);
	off = append_be32(fdt, off, 0x08000000U);
	off = append_be32(fdt, off, 2U);
	/* /cpus/cpu@{0,1} */
	off = append_be32(fdt, off, 1U);
	off = append_name(fdt, off, "cpus");
	off = append_be32(fdt, off, 1U);
	off = append_name(fdt, off, "cpu@0");
	off = append_be32(fdt, off, 2U);
	off = append_be32(fdt, off, 1U);
	off = append_name(fdt, off, "cpu@1");
	off = append_be32(fdt, off, 2U);
	off = append_be32(fdt, off, 2U);
	/* /reserved-memory/framebuffer@42000000 */
	off = append_be32(fdt, off, 1U);
	off = append_name(fdt, off, "reserved-memory");
	off = append_prop_u32(fdt, off, 0U, 2U);
	off = append_prop_u32(fdt, off, 15U, 2U);
	off = append_be32(fdt, off, 1U);
	off = append_name(fdt, off, "framebuffer@42000000");
	off = append_be32(fdt, off, 3U);
	off = append_be32(fdt, off, 16U);
	off = append_be32(fdt, off, 27U);
	off = append_be32(fdt, off, 0U);
	off = append_be32(fdt, off, 0x42000000U);
	off = append_be32(fdt, off, 0U);
	off = append_be32(fdt, off, 0x200000U);
	off = append_be32(fdt, off, 2U);
	off = append_be32(fdt, off, 2U);
	/* Driver-selection resources are described by compatible strings. */
	off = append_be32(fdt, off, 1U);
	off = append_name(fdt, off, "intc@8000000");
	off = append_prop_bytes(fdt, off, 31U, gic_compat,
				(uint32_t)sizeof(gic_compat));
	off = append_be32(fdt, off, 2U);
	off = append_be32(fdt, off, 1U);
	off = append_name(fdt, off, "timer");
	off = append_prop_bytes(fdt, off, 31U, timer_compat,
				(uint32_t)sizeof(timer_compat));
	off = append_be32(fdt, off, 2U);
	off = append_be32(fdt, off, 1U);
	off = append_name(fdt, off, "psci");
	off = append_prop_bytes(fdt, off, 31U, psci_compat,
				(uint32_t)sizeof(psci_compat));
	off = append_prop_bytes(fdt, off, 42U, psci_method,
				(uint32_t)sizeof(psci_method));
	off = append_be32(fdt, off, 2U);
	off = append_be32(fdt, off, 1U);
	off = append_name(fdt, off, "clocks@40018000");
	off = append_prop_bytes(fdt, off, 31U, rp1_compat,
				(uint32_t)sizeof(rp1_compat));
	off = append_be32(fdt, off, 2U);
	off = append_be32(fdt, off, 2U);
	off = append_be32(fdt, off, 9U);
	strings_off = off;
	memcpy(fdt + strings_off, strings, sizeof(strings));
	off += sizeof(strings);

	put_be32(fdt, 0xd00dfeedU);
	put_be32(fdt + 4, (uint32_t)off);
	put_be32(fdt + 8, (uint32_t)struct_off);
	put_be32(fdt + 12, (uint32_t)strings_off);
	put_be32(fdt + 16, 40U);
	put_be32(fdt + 32, (uint32_t)sizeof(strings));
	put_be32(fdt + 36, (uint32_t)(strings_off - struct_off));
	return off;
}

void test_arm64_boot_info_fdt_contract(void)
{
	uint8_t fdt[64] __attribute__((aligned(8)));
	uint8_t platform_fdt[1024] __attribute__((aligned(8)));
	const struct arm64_board_boot_info *info;
	size_t platform_fdt_size;

	memset(fdt, 0, sizeof(fdt));
	put_be32(fdt, 0xd00dfeedU);
	put_be32(fdt + 4, sizeof(fdt));

	TEST_BEGIN("arm64 firmware x0 FDT contract");
	/* A correct header without reservation/tree terminators is not a DTB. */
	ASSERT(arm64_fdt_boot_info_init((uintptr_t)fdt) != 0);
	ASSERT(arm64_board_boot_info()->fdt_valid == 0);
	platform_fdt_size = build_platform_fdt(platform_fdt, sizeof(platform_fdt));
	ASSERT(arm64_fdt_boot_info_init((uintptr_t)platform_fdt) == 0);
	info = arm64_board_boot_info();
	ASSERT(info->fdt_valid == 1);
	ASSERT(info->fdt_pa == (uintptr_t)platform_fdt);
	ASSERT(info->fdt_size == platform_fdt_size);
	ASSERT(info->memory_range_count == 1U);
	ASSERT(info->memory[0].base == 0x40000000ULL);
	ASSERT(info->memory[0].size == 0x08000000ULL);
	ASSERT(info->cpu_count == 2U);
	ASSERT(info->irq_controller == ARM64_IRQ_CONTROLLER_GIC_V2);
	ASSERT(info->psci_conduit == ARM64_PSCI_CONDUIT_SMC);
	ASSERT(info->architected_timer == 1);
	ASSERT(info->rp1_present == 1);
	ASSERT(info->reserved_range_count == 2U);
	ASSERT(info->reserved[0].base == 0x41000000ULL);
	ASSERT(info->reserved[0].size == 0x1000ULL);
	ASSERT(info->reserved[1].base == 0x42000000ULL);
	ASSERT(info->reserved[1].size == 0x200000ULL);
	ASSERT(arm64_board_finalize_memory(0x40080000U, 0x80000U) == 0);
	info = arm64_board_boot_info();
	ASSERT(info->reserved_range_count == 4U);
	ASSERT(info->usable_range_count == 4U);
	ASSERT(info->usable[0].base == 0x40000000ULL);
	ASSERT(info->usable[0].size == 0x80000ULL);
	ASSERT(info->usable[1].base == 0x40100000ULL);
	ASSERT(info->usable[1].size == 0x0f00000ULL);
	ASSERT(info->usable[2].base == 0x41001000ULL);
	ASSERT(info->usable[2].size == 0x0fff000ULL);
	ASSERT(info->usable[3].base == 0x42200000ULL);
	ASSERT(info->usable[3].size == 0x05e00000ULL);
	ASSERT(arm64_board_finalize_memory(0x40080000U, 0x80000U) == 0);
	ASSERT(arm64_board_boot_info()->reserved_range_count == 4U);
	ASSERT(arm64_fdt_boot_info_init((uintptr_t)(fdt + 1)) != 0);
	ASSERT(arm64_board_boot_info()->fdt_valid == 0);
	TEST_END();
}
