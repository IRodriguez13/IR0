/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: test_early_cpio_abi.c
 * Description: Host checks for Linux initramfs newc helpers
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness.h"
#include <ir0/early_cpio.h>
#include <ir0/early_rootfs.h>
#include <stdint.h>

static void put_hex8(uint8_t *dst, uint32_t value)
{
	static const char hex[] = "0123456789abcdef";
	int i;

	for (i = 7; i >= 0; i--)
	{
		dst[i] = (uint8_t)hex[value & 0xfU];
		value >>= 4;
	}
}

void test_early_cpio_abi(void)
{
	uint8_t buf[256];
	struct ir0_early_cpio_member mem;
	char norm[16];
	uint32_t parsed;
	unsigned i;

	TEST_BEGIN("early_cpio_abi");

	ASSERT(ir0_early_cpio_magic((const uint8_t *)"070701"));
	ASSERT(ir0_early_cpio_magic((const uint8_t *)"070702"));
	ASSERT(!ir0_early_cpio_magic((const uint8_t *)"070707"));
	ASSERT(ir0_early_cpio_hex8((const uint8_t *)"000012ac", &parsed));
	ASSERT_EQ(parsed, 0x12ac);
	ASSERT_EQ(ir0_early_cpio_align4(110 + 5), 116);
	ASSERT_EQ(ir0_early_cpio_normalize("./init", norm, sizeof(norm)), 5);
	ASSERT_STR_EQ(norm, "/init");
	ASSERT_EQ(ir0_early_cpio_normalize("tmp", norm, sizeof(norm)), 4);
	ASSERT_STR_EQ(norm, "/tmp");
	ASSERT(ir0_early_cpio_is_trailer("TRAILER!!!"));
	ASSERT(!ir0_early_cpio_is_trailer("init"));

	for (i = 0; i < sizeof(buf); i++)
		buf[i] = 0;
	buf[0] = '0';
	buf[1] = '7';
	buf[2] = '0';
	buf[3] = '7';
	buf[4] = '0';
	buf[5] = '1';
	put_hex8(buf + 14, 0100644);
	put_hex8(buf + 54, 4);
	put_hex8(buf + 94, 5);
	buf[110] = 'i';
	buf[111] = 'n';
	buf[112] = 'i';
	buf[113] = 't';
	buf[114] = 0;
	buf[116] = 'A';
	buf[117] = 'B';
	buf[118] = 'C';
	buf[119] = 'D';
	ASSERT_EQ(ir0_early_cpio_next(buf, sizeof(buf), 0, &mem), 1);
	ASSERT_EQ(mem.filesize, 4);
	ASSERT_EQ(mem.namesize, 5);
	ASSERT_STR_EQ(mem.name, "init");
	ASSERT_EQ(mem.data[0], 'A');
	ASSERT_EQ(mem.data[3], 'D');

	{
		struct ir0_early_stat64 st;

		ir0_early_rootfs_fill_stat(IR0_EARLY_ROOTFS_FILE, 8, &st);
		ASSERT_EQ(st.st_ino, 16);
		ASSERT_EQ(st.st_size, 8);
		ir0_early_rootfs_fill_stat(IR0_EARLY_ROOTFS_FIFO, 0, &st);
		ASSERT_EQ(st.st_ino, 17);
		ASSERT_EQ(st.st_mode, IR0_EARLY_ROOTFS_MODE_FIFO);
		ir0_early_rootfs_fill_stat(IR0_EARLY_ROOTFS_USERDIR, 0, &st);
		ASSERT_EQ(st.st_ino, 18);
		ASSERT_EQ(st.st_mode, IR0_EARLY_ROOTFS_MODE_DIR);
	}

	TEST_END();
}
