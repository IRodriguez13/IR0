/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * vbe_init_from_boot() must keep the firmware framebuffer address and size
 * exact: a truncated base or a wrapped end maps (and lets /dev/fb0 expose)
 * the wrong physical memory.
 */

#include "test_harness.h"

#include <stdint.h>
#include <ir0/boot_memory.h>
#include <ir0/vbe.h>

static uint64_t g_map_first;
static uint64_t g_map_last;
static unsigned long g_map_calls;

int map_page(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags)
{
	(void)flags;
	if (virt_addr != phys_addr)
		return -1;
	if (g_map_calls == 0)
		g_map_first = phys_addr;
	g_map_last = phys_addr;
	g_map_calls++;
	return 0;
}

static uint64_t g_mmio_start;
static uint64_t g_mmio_end;

void resource_register_mmio(uint64_t start, uint64_t end, const char *name)
{
	(void)name;
	g_mmio_start = start;
	g_mmio_end = end;
}

static void map_reset(void)
{
	g_map_first = 0;
	g_map_last = 0;
	g_map_calls = 0;
}

static struct ir0_boot_framebuffer fb32(uint64_t base, uint32_t width,
					uint32_t height, uint32_t pitch)
{
	struct ir0_boot_framebuffer fb = {
		.base = base,
		.size = (uint64_t)pitch * height,
		.width = width,
		.height = height,
		.pitch = pitch,
		.bpp = 32,
		.type = IR0_BOOT_FB_TYPE_RGB,
		.red_pos = 16, .red_size = 8,
		.green_pos = 8, .green_size = 8,
		.blue_pos = 0, .blue_size = 8,
	};

	return fb;
}

void test_vbe_boot_framebuffer_bounds(void)
{
	struct ir0_boot_framebuffer fb;

	TEST_BEGIN("vbe_boot_framebuffer_bounds");

	/* A 64-bit BAR / GOP framebuffer above 4 GiB keeps its address. */
	map_reset();
	fb = fb32(0x100000000ULL, 1024, 768, 4096);
	ASSERT_EQ(vbe_init_from_boot(&fb), 0);
	ASSERT_EQ((uint64_t)vbe_get_fb_phys(), 0x100000000ULL);
	ASSERT_EQ(g_map_first, 0x100000000ULL);
	ASSERT_EQ(g_map_last, 0x100000000ULL + 3U * 1024U * 1024U - 4096U);
	ASSERT_EQ(g_mmio_start, 0x100000000ULL);
	ASSERT_EQ(g_mmio_end, 0x100000000ULL + 3U * 1024U * 1024U - 1U);

	/*
	 * Inside the pre-mapped GRUB window but running past 4 GiB: the end
	 * must not wrap, so every page outside the window gets mapped.
	 */
	map_reset();
	fb = fb32(0xFD000000ULL, 4096, 12288, 16384);
	ASSERT_EQ(vbe_init_from_boot(&fb), 0);
	ASSERT_EQ(vbe_get_fb_size(), 16384U * 12288U);
	ASSERT_EQ(g_map_first, 0xFD000000ULL);
	ASSERT_EQ(g_map_last, 0xFD000000ULL + 16384ULL * 12288ULL - 4096ULL);

	/* A pitch shorter than one row of pixels is a broken descriptor. */
	map_reset();
	fb = fb32(0xE0000000ULL, 1024, 768, 1024);
	ASSERT_EQ(vbe_init_from_boot(&fb), -1);
	ASSERT_EQ(g_map_calls, 0UL);

	TEST_END();
}
