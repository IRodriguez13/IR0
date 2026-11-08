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

static uint64_t g_root_map_last;
static unsigned long g_root_map_calls;

int map_page_in_directory(address_space_root_t root, uint64_t virt_addr,
			  uint64_t phys_addr, uint64_t flags)
{
	(void)flags;
	if (!root || virt_addr != phys_addr)
		return -1;
	g_root_map_last = phys_addr;
	g_root_map_calls++;
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

/*
 * Every process root must map the whole framebuffer: a 2560x1600x32 mode is
 * 16000 KiB, and clipping the supervisor mapping to 4 MiB left the bottom
 * three quarters of the screen unmapped once a user root was active.
 */
void test_vbe_supervisor_map_covers_framebuffer(void)
{
	static uint64_t root_storage[512];
	struct ir0_boot_framebuffer fb;
	const uint64_t base = 0xE0000000ULL;
	const uint64_t size = 2560ULL * 4ULL * 1600ULL;

	TEST_BEGIN("vbe_supervisor_map_covers_framebuffer");

	fb = fb32(base, 2560, 1600, 2560 * 4);
	ASSERT_EQ(vbe_init_from_boot(&fb), 0);
	ASSERT_EQ((uint64_t)vbe_get_fb_size(), size);

	g_root_map_last = 0;
	g_root_map_calls = 0;
	ASSERT_EQ(vbe_map_supervisor(root_storage), 0);
	ASSERT_EQ(g_root_map_calls, (unsigned long)(size / 4096ULL));
	ASSERT_EQ(g_root_map_last, base + size - 4096ULL);

	TEST_END();
}

/*
 * The x86-64 boot tables pre-map only 4 MiB at 0xFD000000 (two 2 MiB pages
 * in boot_x64.asm). A 16000 KiB framebuffer at that address must still get
 * every page past the first 4 MiB mapped into the kernel root.
 */
void test_vbe_boot_premapped_window_is_4mib(void)
{
	struct ir0_boot_framebuffer fb;
	const uint64_t base = 0xFD000000ULL;
	const uint64_t size = 2560ULL * 4ULL * 1600ULL;

	TEST_BEGIN("vbe_boot_premapped_window_is_4mib");

	map_reset();
	fb = fb32(base, 2560, 1600, 2560 * 4);
	ASSERT_EQ(vbe_init_from_boot(&fb), 0);
	ASSERT(g_map_calls != 0UL);
	ASSERT_EQ(g_map_last, base + size - 4096ULL);

	/* A mode that fits the boot window needs no extra mappings. */
	map_reset();
	fb = fb32(base, 1280, 800, 1280 * 4);
	ASSERT_EQ(vbe_init_from_boot(&fb), 0);
	ASSERT_EQ(g_map_calls, 0UL);

	TEST_END();
}
