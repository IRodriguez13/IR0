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

static uint32_t get_be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	       ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static size_t find_property_length(uint8_t *fdt, uint32_t name_offset,
				   uint32_t expected_length)
{
	uint32_t structure_offset = get_be32(fdt + 8U);
	uint32_t structure_size = get_be32(fdt + 36U);
	uint32_t offset;

	for (offset = structure_offset;
	     offset + 12U <= structure_offset + structure_size; offset += 4U)
	{
		if (get_be32(fdt + offset) == 3U &&
		    get_be32(fdt + offset + 4U) == expected_length &&
		    get_be32(fdt + offset + 8U) == name_offset)
		{
			return offset + 4U;
		}
	}
	return 0U;
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

static size_t append_prop_reg2(uint8_t *p, size_t off, uint32_t nameoff,
			       uint64_t base0, uint64_t size0,
			       uint64_t base1, uint64_t size1)
{
	uint64_t values[4] = {base0, size0, base1, size1};
	uint32_t i;

	off = append_be32(p, off, 3U);
	off = append_be32(p, off, 32U);
	off = append_be32(p, off, nameoff);
	for (i = 0; i < 4U; i++)
	{
		off = append_be32(p, off, (uint32_t)(values[i] >> 32));
		off = append_be32(p, off, (uint32_t)values[i]);
	}
	return off;
}

static size_t append_prop_range(uint8_t *p, size_t off, uint32_t nameoff,
				uint64_t child, uint64_t parent, uint64_t size)
{
	uint64_t values[3] = {child, parent, size};
	uint32_t i;

	off = append_be32(p, off, 3U);
	off = append_be32(p, off, 24U);
	off = append_be32(p, off, nameoff);
	for (i = 0; i < 3U; i++)
	{
		off = append_be32(p, off, (uint32_t)(values[i] >> 32));
		off = append_be32(p, off, (uint32_t)values[i]);
	}
	return off;
}

static size_t build_platform_fdt(uint8_t *fdt, size_t capacity)
{
	static const char strings[] =
		"#address-cells\0#size-cells\0reg\0compatible\0method\0ranges\0";
	static const char gic_compat[] = "arm,gic-400\0arm,cortex-a15-gic";
	static const char timer_compat[] = "arm,armv8-timer";
	static const char psci_compat[] = "arm,psci-1.0";
	static const char psci_method[] = "smc";
	static const char rp1_compat[] = "raspberrypi,rp1-clocks";
	static const char uart_compat[] = "arm,pl011";
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
	/* Exercise child-bus to CPU-physical address translation. */
	off = append_be32(fdt, off, 1U);
	off = append_name(fdt, off, "soc");
	off = append_prop_u32(fdt, off, 0U, 2U);
	off = append_prop_u32(fdt, off, 15U, 2U);
	off = append_prop_range(fdt, off, 49U, 0x08000000U,
				0x108000000ULL, 0x20000U);
	/* Driver-selection resources are described by compatible strings. */
	off = append_be32(fdt, off, 1U);
	off = append_name(fdt, off, "intc@8000000");
	off = append_prop_bytes(fdt, off, 31U, gic_compat,
				(uint32_t)sizeof(gic_compat));
	off = append_prop_reg2(fdt, off, 27U, 0x08000000U, 0x10000U,
			       0x08010000U, 0x10000U);
	off = append_be32(fdt, off, 2U);
	off = append_be32(fdt, off, 2U);
	off = append_be32(fdt, off, 1U);
	off = append_name(fdt, off, "serial@9000000");
	off = append_prop_bytes(fdt, off, 31U, uart_compat,
				(uint32_t)sizeof(uart_compat));
	off = append_be32(fdt, off, 3U);
	off = append_be32(fdt, off, 16U);
	off = append_be32(fdt, off, 27U);
	off = append_be32(fdt, off, 0U);
	off = append_be32(fdt, off, 0x09000000U);
	off = append_be32(fdt, off, 0U);
	off = append_be32(fdt, off, 0x1000U);
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
	uint8_t malformed_fdt[1024] __attribute__((aligned(8)));
	const struct arm64_board_boot_info *info;
	size_t property_length_offset;
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
	ASSERT(info->irq_range_count == 2U);
	ASSERT(info->irq_mmio[0].base == 0x108000000ULL);
	ASSERT(info->irq_mmio[0].size == 0x10000ULL);
	ASSERT(info->irq_mmio[1].base == 0x108010000ULL);
	ASSERT(info->irq_mmio[1].size == 0x10000ULL);
	ASSERT(info->console_uart == ARM64_UART_PL011);
	ASSERT(info->console_mmio.base == 0x09000000ULL);
	ASSERT(info->console_mmio.size == 0x1000ULL);
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
	memcpy(malformed_fdt, platform_fdt, platform_fdt_size);
	property_length_offset = find_property_length(malformed_fdt, 27U, 16U);
	ASSERT(property_length_offset != 0U);
	put_be32(malformed_fdt + property_length_offset, 15U);
	ASSERT(arm64_fdt_boot_info_init((uintptr_t)malformed_fdt) != 0);
	ASSERT(arm64_board_boot_info()->fdt_valid == 0);
	memcpy(malformed_fdt, platform_fdt, platform_fdt_size);
	property_length_offset = find_property_length(malformed_fdt, 27U, 16U);
	ASSERT(property_length_offset != 0U);
	put_be32(malformed_fdt + property_length_offset, 0xfffffff0U);
	ASSERT(arm64_fdt_boot_info_init((uintptr_t)malformed_fdt) != 0);
	ASSERT(arm64_board_boot_info()->fdt_valid == 0);
	memcpy(malformed_fdt, platform_fdt, platform_fdt_size);
	put_be32(malformed_fdt + 16U, (uint32_t)platform_fdt_size - 8U);
	ASSERT(arm64_fdt_boot_info_init((uintptr_t)malformed_fdt) != 0);
	ASSERT(arm64_board_boot_info()->fdt_valid == 0);
	ASSERT(arm64_fdt_boot_info_init((uintptr_t)(fdt + 1)) != 0);
	ASSERT(arm64_board_boot_info()->fdt_valid == 0);
	/* x0 empty, DTB sitting at the fallback address. */
	ASSERT(arm64_fdt_boot_info_init_fallback(0, (uintptr_t)platform_fdt) == 0);
	ASSERT(arm64_board_boot_info()->fdt_valid == 1);
	ASSERT(arm64_board_boot_info()->fdt_pa == (uintptr_t)platform_fdt);
	/* A valid x0 wins over a bad fallback. */
	ASSERT(arm64_fdt_boot_info_init_fallback((uintptr_t)platform_fdt, 1) == 0);
	ASSERT(arm64_board_boot_info()->fdt_pa == (uintptr_t)platform_fdt);
	ASSERT(arm64_fdt_boot_info_init_fallback(0, 0) != 0);
	ASSERT(arm64_board_boot_info()->fdt_valid == 0);
	TEST_END();
}

void test_arm64_boot_info_genet_fdt(void)
{
	static const char strings[] =
		"#address-cells\0#size-cells\0reg\0compatible\0interrupts\0"
		"phy-mode\0local-mac-address\0";
	static const char genet_compat[] = "brcm,bcm2711-genet-v5";
	static const char phy_mode[] = "rgmii-rxid";
	static const uint8_t mac[6] = { 0xdc, 0xa6, 0x32, 0x00, 0x00, 0x01 };
	uint8_t fdt[512] __attribute__((aligned(8)));
	const struct arm64_board_boot_info *info;
	const size_t struct_off = 72U;
	size_t off = struct_off;
	size_t strings_off;

	memset(fdt, 0, sizeof(fdt));
	off = append_be32(fdt, off, 1U);
	off = append_name(fdt, off, "");
	off = append_prop_u32(fdt, off, 0U, 2U);
	off = append_prop_u32(fdt, off, 15U, 2U);
	off = append_be32(fdt, off, 1U);
	off = append_name(fdt, off, "ethernet@fd580000");
	off = append_prop_bytes(fdt, off, 31U, genet_compat,
				(uint32_t)sizeof(genet_compat));
	off = append_be32(fdt, off, 3U);
	off = append_be32(fdt, off, 16U);
	off = append_be32(fdt, off, 27U);
	off = append_be32(fdt, off, 0U);
	off = append_be32(fdt, off, 0xfd580000U);
	off = append_be32(fdt, off, 0U);
	off = append_be32(fdt, off, 0x10000U);
	off = append_be32(fdt, off, 3U);
	off = append_be32(fdt, off, 24U);
	off = append_be32(fdt, off, 42U);
	off = append_be32(fdt, off, 0U);
	off = append_be32(fdt, off, 157U);
	off = append_be32(fdt, off, 4U);
	off = append_be32(fdt, off, 0U);
	off = append_be32(fdt, off, 158U);
	off = append_be32(fdt, off, 4U);
	off = append_prop_bytes(fdt, off, 53U, phy_mode,
				(uint32_t)sizeof(phy_mode));
	off = append_prop_bytes(fdt, off, 62U, mac, 6U);
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

	TEST_BEGIN("arm64 genet FDT discovery");
	ASSERT(arm64_fdt_boot_info_init((uintptr_t)fdt) == 0);
	info = arm64_board_boot_info();
	ASSERT(info->genet_present == 1);
	ASSERT(info->genet_mmio.base == 0xfd580000ULL);
	ASSERT(info->genet_mmio.size == 0x10000ULL);
	ASSERT_EQ(info->genet_irq_count, 2U);
	ASSERT_EQ(info->genet_irq[0], 189U);
	ASSERT_EQ(info->genet_irq[1], 190U);
	ASSERT_EQ(info->genet_phy_addr, 1U);
	ASSERT_STR_EQ(info->genet_phy_mode, "rgmii-rxid");
	ASSERT_EQ(info->genet_mac[0], (uint8_t)0xdc);
	TEST_END();
}
