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

	ir0_early_rootfs_fill_stat(IR0_EARLY_ROOTFS_EVENTFD, 0, &st);
	ASSERT_EQ(st.st_ino, 10);
	ASSERT_EQ(st.st_mode, IR0_EARLY_ROOTFS_MODE_REG);

	ir0_early_rootfs_fill_stat(IR0_EARLY_ROOTFS_PIPE_R, 0, &st);
	ASSERT_EQ(st.st_ino, 11);
	ASSERT_EQ(st.st_mode, IR0_EARLY_ROOTFS_MODE_FIFO);

	ir0_early_rootfs_fill_stat(IR0_EARLY_ROOTFS_MEMFD, 64, &st);
	ASSERT_EQ(st.st_ino, 12);
	ASSERT_EQ(st.st_mode, IR0_EARLY_ROOTFS_MODE_REG);
	ASSERT_EQ(st.st_size, 64);

	ir0_early_rootfs_fill_stat(IR0_EARLY_ROOTFS_TIMERFD, 0, &st);
	ASSERT_EQ(st.st_ino, 13);
	ASSERT_EQ(st.st_mode, IR0_EARLY_ROOTFS_MODE_REG);

	ir0_early_rootfs_fill_stat(IR0_EARLY_ROOTFS_EPOLL, 0, &st);
	ASSERT_EQ(st.st_ino, 14);
	ASSERT_EQ(st.st_mode, IR0_EARLY_ROOTFS_MODE_REG);

	ir0_early_rootfs_fill_stat(IR0_EARLY_ROOTFS_SOCK, 0, &st);
	ASSERT_EQ(st.st_ino, 15);
	ASSERT_EQ(st.st_mode, IR0_EARLY_ROOTFS_MODE_SOCK);

	ir0_early_rootfs_fill_stat(IR0_EARLY_ROOTFS_UNIX, 0, &st);
	ASSERT_EQ(st.st_ino, 19);
	ASSERT_EQ(st.st_mode, IR0_EARLY_ROOTFS_MODE_SOCK);
	ASSERT_EQ(IR0_EARLY_DT_SOCK, 12);

	ir0_early_rootfs_fill_stat(IR0_EARLY_ROOTFS_SYMLINK, 4, &st);
	ASSERT_EQ(st.st_ino, 20);
	ASSERT_EQ(st.st_mode, IR0_EARLY_ROOTFS_MODE_LNK);
	ASSERT_EQ(st.st_size, 4);
	ASSERT_EQ(IR0_EARLY_DT_LNK, 10);
	ASSERT_EQ(IR0_EARLY_MSGHDR_BYTES, 56);
	{
		uint8_t hdr[IR0_EARLY_MSGHDR_BYTES];
		unsigned i;

		for (i = 0; i < sizeof(hdr); i++)
			hdr[i] = 0;
		ASSERT(!ir0_early_msghdr_has_cmsg(hdr, sizeof(hdr)));
		hdr[32] = 1;
		ASSERT(ir0_early_msghdr_has_cmsg(hdr, sizeof(hdr)));
	}

	ASSERT(ir0_early_timerfd_clock_ok(IR0_EARLY_CLOCK_MONOTONIC));
	ASSERT(ir0_early_timerfd_clock_ok(IR0_EARLY_CLOCK_REALTIME));
	ASSERT(!ir0_early_timerfd_clock_ok(2));
	ASSERT(ir0_early_timerfd_create_flags_ok(IR0_EARLY_TFD_CLOEXEC |
						IR0_EARLY_TFD_NONBLOCK));
	ASSERT(!ir0_early_timerfd_create_flags_ok(0x10));
	ASSERT(ir0_early_memfd_flags_ok(IR0_EARLY_MFD_CLOEXEC |
				       IR0_EARLY_MFD_ALLOW_SEALING));
	ASSERT(!ir0_early_memfd_flags_ok(IR0_EARLY_MFD_HUGETLB));
	ASSERT(ir0_early_epoll_create_flags_ok(IR0_EARLY_EPOLL_CLOEXEC));
	ASSERT(!ir0_early_epoll_create_flags_ok(2));
	ASSERT(ir0_early_socketpair_args_ok(IR0_EARLY_AF_UNIX,
					   IR0_EARLY_SOCK_STREAM, 0));
	ASSERT(ir0_early_socketpair_args_ok(IR0_EARLY_AF_UNIX,
					   IR0_EARLY_SOCK_DGRAM |
						   IR0_EARLY_SOCK_CLOEXEC,
					   0));
	ASSERT(!ir0_early_socketpair_args_ok(2, IR0_EARLY_SOCK_STREAM, 0));
	ASSERT(ir0_early_socket_args_ok(IR0_EARLY_AF_UNIX, IR0_EARLY_SOCK_STREAM,
				       0));
	ASSERT(!ir0_early_socket_args_ok(2, IR0_EARLY_SOCK_STREAM, 0));
	{
		uint8_t sun[16];
		char path[16];
		unsigned i;

		for (i = 0; i < sizeof(sun); i++)
			sun[i] = 0;
		sun[0] = IR0_EARLY_AF_UNIX;
		sun[2] = '/';
		sun[3] = 't';
		sun[4] = 'm';
		sun[5] = 'p';
		sun[6] = '/';
		sun[7] = 's';
		ASSERT(ir0_early_sockaddr_un_path(sun, 8, path, sizeof(path)));
		ASSERT_STR_EQ(path, "/tmp/s");
		sun[2] = 0;
		ASSERT(!ir0_early_sockaddr_un_path(sun, 8, path, sizeof(path)));
		sun[0] = 2;
		sun[2] = '/';
		ASSERT(!ir0_early_sockaddr_un_path(sun, 8, path, sizeof(path)));
	}
	ASSERT_EQ(IR0_EARLY_EPOLL_EVENT_BYTES, 16);
	ASSERT_EQ(IR0_EARLY_ITIMERSPEC_BYTES, 32);
	ASSERT_EQ(ir0_early_cmsg_align(20), 24);
	ASSERT(ir0_early_renameat2_flags_ok(0));
	ASSERT(ir0_early_renameat2_flags_ok(IR0_EARLY_RENAME_NOREPLACE));
	ASSERT(ir0_early_renameat2_flags_ok(IR0_EARLY_RENAME_EXCHANGE));
	ASSERT(!ir0_early_renameat2_flags_ok(IR0_EARLY_RENAME_NOREPLACE |
					    IR0_EARLY_RENAME_EXCHANGE));
	ASSERT(!ir0_early_renameat2_flags_ok(IR0_EARLY_RENAME_WHITEOUT));
	ASSERT_EQ(IR0_EARLY_STATX_BYTES, 256);
	ASSERT_EQ(IR0_EARLY_STATX_BASIC, 0x7ff);
	{
		struct ir0_early_stat64 sxst;
		uint8_t sx[IR0_EARLY_STATX_BYTES];

		ir0_early_rootfs_fill_stat(IR0_EARLY_ROOTFS_CONSOLE, 0, &sxst);
		ir0_early_statx_from_stat(&sxst, sx);
		ASSERT_EQ(sx[0], 0xff);
		ASSERT_EQ(sx[1], 0x07);
		ASSERT_EQ(sx[28], (uint8_t)(IR0_EARLY_ROOTFS_MODE_CHR & 0xff));
		ASSERT_EQ(sx[128], 5);
		ASSERT_EQ(sx[132], 1);
	}
	ASSERT_EQ(ir0_early_timespec_to_ticks(1, 0, 1000), 1000);
	ASSERT_EQ(ir0_early_timespec_to_ticks(0, 500000000, 1000), 500);

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

	{
		uint8_t elf[128];
		unsigned i;

		for (i = 0; i < sizeof(elf); i++)
			elf[i] = 0;
		ASSERT_EQ(ir0_early_elf64_image_bytes(elf, sizeof(elf)), 0);
		elf[0] = 0x7f;
		elf[1] = 'E';
		elf[2] = 'L';
		elf[3] = 'F';
		elf[4] = 2;
		elf[5] = 1;
		elf[32] = 64;
		elf[54] = 56;
		elf[56] = 1;
		elf[64] = 1; /* PT_LOAD */
		elf[64 + 8] = 0;
		elf[64 + 32] = 120;
		ASSERT_EQ(ir0_early_elf64_image_bytes(elf, sizeof(elf)), 120);
	}

	TEST_END();
}
