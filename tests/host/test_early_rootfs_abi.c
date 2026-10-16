/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_early_rootfs_abi.c
 * Description: Host checks for staged "/", "/init" and Linux /dev char nodes
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness.h"
#include <ir0/early_rootfs.h>
#include <stdint.h>

void test_early_rootfs_abi(void)
{
	struct ir0_early_stat64 st;

	TEST_BEGIN("early_rootfs_abi");

	ASSERT_EQ(ir0_early_rootfs_path_node("/"), IR0_EARLY_ROOTFS_ROOT);
	ASSERT_EQ(ir0_early_rootfs_path_node("/init"), IR0_EARLY_ROOTFS_INIT);
	ASSERT_EQ(ir0_early_rootfs_path_node("/dev"), IR0_EARLY_ROOTFS_DEV);
	ASSERT_EQ(ir0_early_rootfs_path_node("/dev/console"),
		  IR0_EARLY_ROOTFS_CONSOLE);
	ASSERT_EQ(ir0_early_rootfs_path_node("/dev/null"),
		  IR0_EARLY_ROOTFS_NULL);
	ASSERT_EQ(ir0_early_rootfs_path_node("/dev/zero"),
		  IR0_EARLY_ROOTFS_ZERO);
	ASSERT_EQ(ir0_early_rootfs_path_node("/dev/tty"),
		  IR0_EARLY_ROOTFS_TTY);
	ASSERT_EQ(ir0_early_rootfs_path_node("/dev/vda"),
		  IR0_EARLY_ROOTFS_VDA);
	ASSERT_EQ(ir0_early_rootfs_path_node("/tmp"), IR0_EARLY_ROOTFS_TMP);
	ASSERT_EQ(ir0_early_rootfs_path_node("/bin/sh"), IR0_EARLY_ROOTFS_NONE);
	ASSERT_EQ(ir0_early_rootfs_path_node(0), IR0_EARLY_ROOTFS_NONE);

	ir0_early_rootfs_fill_stat(IR0_EARLY_ROOTFS_INIT, 4096, &st);
	ASSERT_EQ(st.st_ino, 2);
	ASSERT_EQ(st.st_mode, IR0_EARLY_ROOTFS_MODE_REG);
	ASSERT_EQ(st.st_size, 4096);

	ir0_early_rootfs_fill_stat(IR0_EARLY_ROOTFS_ROOT, 4096, &st);
	ASSERT_EQ(st.st_ino, 1);
	ASSERT_EQ(st.st_mode, IR0_EARLY_ROOTFS_MODE_DIR);
	ASSERT_EQ(st.st_size, 0);

	ir0_early_rootfs_fill_stat(IR0_EARLY_ROOTFS_DEV, 0, &st);
	ASSERT_EQ(st.st_ino, 3);
	ASSERT_EQ(st.st_mode, IR0_EARLY_ROOTFS_MODE_DIR);

	ir0_early_rootfs_fill_stat(IR0_EARLY_ROOTFS_CONSOLE, 0, &st);
	ASSERT_EQ(st.st_ino, 4);
	ASSERT_EQ(st.st_mode, IR0_EARLY_ROOTFS_MODE_CHR);
	ASSERT_EQ(st.st_rdev, IR0_EARLY_ROOTFS_CONSOLE_RDEV);

	ir0_early_rootfs_fill_stat(IR0_EARLY_ROOTFS_NULL, 0, &st);
	ASSERT_EQ(st.st_ino, 5);
	ASSERT_EQ(st.st_rdev, IR0_EARLY_ROOTFS_NULL_RDEV);

	ir0_early_rootfs_fill_stat(IR0_EARLY_ROOTFS_ZERO, 0, &st);
	ASSERT_EQ(st.st_ino, 6);
	ASSERT_EQ(st.st_rdev, IR0_EARLY_ROOTFS_ZERO_RDEV);

	ir0_early_rootfs_fill_stat(IR0_EARLY_ROOTFS_TTY, 0, &st);
	ASSERT_EQ(st.st_ino, 7);
	ASSERT_EQ(st.st_rdev, IR0_EARLY_ROOTFS_TTY_RDEV);

	ir0_early_rootfs_fill_stat(IR0_EARLY_ROOTFS_VDA, 512, &st);
	ASSERT_EQ(st.st_ino, 8);
	ASSERT_EQ(st.st_mode, IR0_EARLY_ROOTFS_MODE_BLK);
	ASSERT_EQ(st.st_rdev, IR0_EARLY_ROOTFS_VDA_RDEV);
	ASSERT_EQ(st.st_size, 512);

	ir0_early_rootfs_fill_stat(IR0_EARLY_ROOTFS_TMP, 0, &st);
	ASSERT_EQ(st.st_ino, 9);
	ASSERT_EQ(st.st_mode, IR0_EARLY_ROOTFS_MODE_DIR);

	ASSERT_EQ(ir0_early_rootfs_dir_count(IR0_EARLY_ROOTFS_ROOT), 5);
	ASSERT_EQ(ir0_early_rootfs_dir_count(IR0_EARLY_ROOTFS_TMP), 2);
	ASSERT_EQ(ir0_early_rootfs_dir_count(IR0_EARLY_ROOTFS_DEV), 6);
	ASSERT_EQ(ir0_early_rootfs_dir_count(IR0_EARLY_ROOTFS_NULL), 0);
	{
		struct ir0_early_dirent_spec spec;
		uint8_t rec[32];
		unsigned reclen;

		spec = ir0_early_rootfs_dir_entry(IR0_EARLY_ROOTFS_ROOT, 2);
		ASSERT_EQ(spec.ino, 2);
		ASSERT_EQ(spec.type, IR0_EARLY_DT_REG);
		ASSERT_STR_EQ(spec.name, "init");
		reclen = ir0_early_dirent64_pack(rec, sizeof(rec), spec.ino, 3,
						spec.type, spec.name);
		ASSERT(reclen >= 24);
		ASSERT_EQ(rec[18], IR0_EARLY_DT_REG);
		spec = ir0_early_rootfs_dir_entry(IR0_EARLY_ROOTFS_DEV, 3);
		ASSERT_EQ(spec.ino, 5);
		ASSERT_EQ(spec.type, IR0_EARLY_DT_CHR);
	}

	TEST_END();
}
