/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: linux_abi_fs.c
 * Description: RED selftest (fails on dev 629036ef, not wired into any gate):
 *              file and path syscalls that diverge from Linux, found by the
 *              conformance harness (Documentation/conformance.md). One case
 *              per bug; each passes on Linux. Built with the conformance
 *              runner (tests/conformance) and run by
 *              make ir0-selftest-red-fs-{x86,arm64}.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "conf.h"

#include <dirent.h>
#include <sys/stat.h>

/* fs/namei.c getname_flags(): an empty path is ENOENT without AT_EMPTY_PATH. */
int fs_empty_path_enoent(void)
{
	struct stat st;

	EXPECT_ERR(S4(SYS_openat, AT_FDCWD, "", O_RDONLY, 0), ENOENT);
	EXPECT_ERR(S4(SYS_newfstatat, AT_FDCWD, "", &st, 0), ENOENT);
	return CONF_PASS;
}

/* mkdir(2): a missing path component is ENOENT, not EACCES. */
int fs_mkdir_missing_parent_enoent(void)
{
	EXPECT_ERR(S3(SYS_mkdirat, AT_FDCWD, "missing/d", 0755), ENOENT);
	return CONF_PASS;
}

/* lseek(2): ESPIPE on a pipe. */
int fs_lseek_pipe_espipe(void)
{
	int p[2];

	EXPECT(S2(SYS_pipe2, p, 0), 0);
	EXPECT_ERR(S3(SYS_lseek, p[0], 0, SEEK_SET), ESPIPE);
	return CONF_PASS;
}

/* fcntl(2) F_SETFL may change O_APPEND. */
int fs_setfl_append_sticks(void)
{
	long fd = conf_creat("f", 0);

	CHECK(fd >= 0);
	EXPECT(S3(SYS_fcntl, fd, F_SETFL, O_APPEND), 0);
	EXPECT(S2(SYS_fcntl, fd, F_GETFL) & O_APPEND, O_APPEND);
	return CONF_PASS;
}

/* getdents64(2) lists "." and ".." in every directory. */
int fs_getdents64_lists_dot_entries(void)
{
	char buf[512];
	int seen = 0;
	long fd;
	long n;
	long off;

	fd = S4(SYS_openat, AT_FDCWD, ".", O_RDONLY | O_DIRECTORY, 0);
	CHECK(fd >= 0);
	while ((n = S3(SYS_getdents64, fd, buf, sizeof(buf))) > 0)
	{
		for (off = 0; off < n;)
		{
			struct dirent *d = (struct dirent *)(buf + off);

			if (d->d_reclen == 0)
				break;
			if (rt_streq(d->d_name, "."))
				seen |= 1;
			if (rt_streq(d->d_name, ".."))
				seen |= 2;
			off += d->d_reclen;
		}
	}
	EXPECT(seen, 3);
	return CONF_PASS;
}

/* fstatat(2) with AT_SYMLINK_NOFOLLOW describes the link itself. */
int fs_lstat_symlink(void)
{
	struct stat st;
	long fd = conf_creat("target", "x");

	CHECK(fd >= 0);
	EXPECT(S3(SYS_symlinkat, "target", AT_FDCWD, "link"), 0);
	EXPECT(S4(SYS_newfstatat, AT_FDCWD, "link", &st, AT_SYMLINK_NOFOLLOW), 0);
	CHECK(S_ISLNK(st.st_mode));
	return CONF_PASS;
}

/* fs/d_path.c sys_getcwd(): returns the length including the NUL. */
int fs_getcwd_counts_nul(void)
{
	char buf[256];
	long n = S2(SYS_getcwd, buf, sizeof(buf));

	CHECK(n > 0);
	EXPECT(n, (long)rt_strlen(buf) + 1);
	return CONF_PASS;
}

/* execve(2): a regular file without execute permission is EACCES. */
int fs_execve_non_executable_eacces(void)
{
	char *argv[2] = { (char *)"noexec", 0 };
	long fd = conf_creat("noexec", "#!/bin/sh\n");

	CHECK(fd >= 0);
	EXPECT(S1(SYS_close, fd), 0);
	EXPECT_ERR(S3(SYS_execve, "noexec", argv, 0), EACCES);
	return CONF_PASS;
}

/*
 * fstat(2) fills the ISA's struct stat: arm64 uses the asm-generic layout
 * (st_mode before st_nlink, both 32-bit), x86_64 its own.
 */
int fs_stat_layout_matches_isa(void)
{
	struct stat st;
	long fd;

	(void)S1(SYS_umask, 0);
	fd = S4(SYS_openat, AT_FDCWD, "f", O_CREAT | O_RDWR, 0640);
	CHECK(fd >= 0);
	EXPECT(S3(SYS_write, fd, "abc", 3), 3);
	EXPECT(S2(SYS_fstat, fd, &st), 0);
	CHECK(S_ISREG(st.st_mode));
	EXPECT(st.st_mode & 07777, 0640);
	EXPECT(st.st_nlink, 1);
	EXPECT(st.st_size, 3);
	return CONF_PASS;
}
