/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: linux_abi_fs.c
 * Description: Selftest: file and path syscalls that diverged from Linux,
 *              found by the conformance harness (Documentation/conformance.md).
 *              One case per fixed bug; each passes on Linux and on IR0. Run
 *              by make ir0-selftest-fs-abi-{x86,arm64}.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "conf.h"

#include <dirent.h>
#include <sys/stat.h>

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif
#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 1U
#endif

#define WAIT_MS 5000L
/* fs/exec.c: one string, NUL included, may be 32 pages long. */
#define MAX_ARG_STRLEN (32L * 4096L)

static char g_arg[MAX_ARG_STRLEN + 1];

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

/* A regular file used as a directory component is ENOTDIR, not ENOENT. */
int fs_file_component_enotdir(void)
{
	struct stat st;
	long fd = conf_creat("f", 0);

	CHECK(fd >= 0);
	EXPECT_ERR(S4(SYS_openat, AT_FDCWD, "f/x", O_RDONLY, 0), ENOTDIR);
	EXPECT_ERR(S4(SYS_newfstatat, AT_FDCWD, "f/x", &st, 0), ENOTDIR);
	return CONF_PASS;
}

/* rmdir(2) / unlinkat(AT_REMOVEDIR) refuse a non-empty directory. */
int fs_rmdir_nonempty_enotempty(void)
{
	long fd;

	EXPECT(S3(SYS_mkdirat, AT_FDCWD, "d", 0755), 0);
	fd = conf_creat("d/f", 0);
	CHECK(fd >= 0);
	EXPECT(S1(SYS_close, fd), 0);
	EXPECT_ERR(S3(SYS_unlinkat, AT_FDCWD, "d", AT_REMOVEDIR), ENOTEMPTY);
	fd = S4(SYS_openat, AT_FDCWD, "d/f", O_RDONLY, 0);
	CHECK(fd >= 0);
	return CONF_PASS;
}

static void fill_arg(long len)
{
	long i;

	for (i = 0; i < len; i++)
		g_arg[i] = 'a';
	g_arg[len] = '\0';
}

/* execve(2): an argument longer than 255 bytes reaches the new image. */
int fs_execve_long_arg(void)
{
	char num[24];
	char *argv[6];
	int status = 0;
	long pid;
	long ret;

	fill_arg(1000);
	num[0] = '\0';
	rt_catn(num, sizeof(num), 1000);
	argv[0] = (char *)conf_self;
	argv[1] = (char *)"--probe";
	argv[2] = (char *)"arglen";
	argv[3] = num;
	argv[4] = g_arg;
	argv[5] = 0;
	pid = rt_fork();
	CHECK(pid >= 0);
	if (pid == 0)
	{
		ret = S3(SYS_execve, conf_self, argv, 0);
		rt_exit(ret == -EFAULT ? 122 : 121);
	}
	EXPECT(conf_child_status(pid, &status, WAIT_MS), pid);
	CHECK(RT_EXITED(status));
	EXPECT(RT_EXITCODE(status), 0);
	return CONF_PASS;
}

/*
 * fs/exec.c copy_strings(): a string over MAX_ARG_STRLEN, or vectors larger
 * than the argument space, are E2BIG. 64 strings of 128 KiB exceed the
 * limit Linux derives from any stack rlimit (at most 6 MiB).
 */
int fs_execve_e2big(void)
{
	char *argv[66];
	int i;

	fill_arg(MAX_ARG_STRLEN);
	argv[0] = (char *)conf_self;
	argv[1] = g_arg;
	argv[2] = 0;
	EXPECT_ERR(S3(SYS_execve, conf_self, argv, 0), E2BIG);

	fill_arg(MAX_ARG_STRLEN - 1);
	for (i = 1; i < 65; i++)
		argv[i] = g_arg;
	argv[65] = 0;
	EXPECT_ERR(S3(SYS_execve, conf_self, argv, 0), E2BIG);
	return CONF_PASS;
}

/* open(2) O_NOFOLLOW: a symlink as the last component is ELOOP. */
int fs_open_nofollow_eloop(void)
{
	long fd = conf_creat("target", "x");

	CHECK(fd >= 0);
	EXPECT(S3(SYS_symlinkat, "target", AT_FDCWD, "link"), 0);
	EXPECT_ERR(S4(SYS_openat, AT_FDCWD, "link", O_RDONLY | O_NOFOLLOW, 0),
		   ELOOP);
	return CONF_PASS;
}

/*
 * open(2) O_DIRECTORY on a regular file is ENOTDIR. The flag value is per
 * ISA (x86_64 0200000, arm64 040000).
 */
int fs_open_directory_on_file_enotdir(void)
{
	long fd = conf_creat("f", 0);

	CHECK(fd >= 0);
	EXPECT_ERR(S4(SYS_openat, AT_FDCWD, "f", O_RDONLY | O_DIRECTORY, 0),
		   ENOTDIR);
	return CONF_PASS;
}

/* unlink(2) of a directory is EISDIR and leaves it in place. */
int fs_unlink_dir_eisdir(void)
{
	struct stat st;

	EXPECT(S3(SYS_mkdirat, AT_FDCWD, "d", 0755), 0);
	EXPECT_ERR(S3(SYS_unlinkat, AT_FDCWD, "d", 0), EISDIR);
	EXPECT(S4(SYS_newfstatat, AT_FDCWD, "d", &st, 0), 0);
	CHECK(S_ISDIR(st.st_mode));
	return CONF_PASS;
}

/* open(2): a directory opens only read-only; O_CREAT never replaces it. */
int fs_open_dir_for_write_eisdir(void)
{
	struct stat st;

	EXPECT(S3(SYS_mkdirat, AT_FDCWD, "d", 0755), 0);
	EXPECT_ERR(S4(SYS_openat, AT_FDCWD, "d", O_WRONLY, 0), EISDIR);
	EXPECT_ERR(S4(SYS_openat, AT_FDCWD, "d", O_RDWR | O_CREAT, 0644), EISDIR);
	EXPECT(S4(SYS_newfstatat, AT_FDCWD, "d", &st, 0), 0);
	CHECK(S_ISDIR(st.st_mode));
	return CONF_PASS;
}

/* Create @name with @data and close it. */
static int make_file(const char *name, const char *data)
{
	long fd = conf_creat(name, data);

	if (fd < 0)
		return (int)fd;
	return (int)S1(SYS_close, fd);
}

/* Nonzero when @name exists (lstat semantics). */
static int exists(const char *name)
{
	struct stat st;

	return S4(SYS_newfstatat, AT_FDCWD, name, &st, AT_SYMLINK_NOFOLLOW) == 0;
}

/* Read up to @cap bytes of @name; returns the count or -errno. */
static long read_file(const char *name, char *buf, long cap)
{
	long fd = S4(SYS_openat, AT_FDCWD, name, O_RDONLY, 0);
	long n;

	if (fd < 0)
		return fd;
	n = S3(SYS_read, fd, buf, cap);
	(void)S1(SYS_close, fd);
	return n;
}

/*
 * rename(2): a file over a directory is EISDIR, and the directory and its
 * contents stay. IR0 deleted the directory tree and renamed the file in.
 */
int fs_rename_file_over_dir_eisdir(void)
{
	EXPECT(make_file("f", "x"), 0);
	EXPECT(S3(SYS_mkdirat, AT_FDCWD, "d", 0755), 0);
	EXPECT(make_file("d/keep", "data"), 0);
	EXPECT_ERR(S4(SYS_renameat, AT_FDCWD, "f", AT_FDCWD, "d"), EISDIR);
	CHECK(exists("d/keep"));
	CHECK(exists("f"));
	return CONF_PASS;
}

/* rename(2): a directory over a non-directory is ENOTDIR. */
int fs_rename_dir_over_file_enotdir(void)
{
	char buf[8];

	EXPECT(S3(SYS_mkdirat, AT_FDCWD, "d", 0755), 0);
	EXPECT(make_file("f", "data"), 0);
	EXPECT_ERR(S4(SYS_renameat, AT_FDCWD, "d", AT_FDCWD, "f"), ENOTDIR);
	EXPECT(read_file("f", buf, sizeof(buf)), 4);
	CHECK(exists("d"));
	return CONF_PASS;
}

/* rename(2): a directory over a non-empty directory is ENOTEMPTY. */
int fs_rename_dir_over_nonempty_dir_enotempty(void)
{
	EXPECT(S3(SYS_mkdirat, AT_FDCWD, "a", 0755), 0);
	EXPECT(S3(SYS_mkdirat, AT_FDCWD, "b", 0755), 0);
	EXPECT(make_file("b/keep", "data"), 0);
	EXPECT_ERR(S4(SYS_renameat, AT_FDCWD, "a", AT_FDCWD, "b"), ENOTEMPTY);
	CHECK(exists("b/keep"));
	CHECK(exists("a"));
	return CONF_PASS;
}

/* rename(2) of a file onto itself succeeds and changes nothing. */
int fs_rename_onto_itself_keeps_file(void)
{
	char buf[8];

	EXPECT(make_file("f", "data"), 0);
	EXPECT(S4(SYS_renameat, AT_FDCWD, "f", AT_FDCWD, "f"), 0);
	EXPECT(read_file("f", buf, sizeof(buf)), 4);
	CHECK(rt_memeq(buf, "data", 4));
	return CONF_PASS;
}

/*
 * mknod(2) over an existing path is EEXIST, whatever is there. IR0 removed
 * the file or the whole directory tree to make room for the FIFO.
 */
int fs_mknod_existing_eexist(void)
{
	char buf[8];
	struct stat st;

	EXPECT(make_file("f", "data"), 0);
	EXPECT_ERR(S4(SYS_mknodat, AT_FDCWD, "f", S_IFIFO | 0600, 0), EEXIST);
	EXPECT(read_file("f", buf, sizeof(buf)), 4);
	EXPECT(S3(SYS_mkdirat, AT_FDCWD, "d", 0755), 0);
	EXPECT(make_file("d/keep", "data"), 0);
	EXPECT_ERR(S4(SYS_mknodat, AT_FDCWD, "d", S_IFIFO | 0600, 0), EEXIST);
	CHECK(exists("d/keep"));
	EXPECT(S4(SYS_mknodat, AT_FDCWD, "p", S_IFIFO | 0600, 0), 0);
	EXPECT_ERR(S4(SYS_mknodat, AT_FDCWD, "p", S_IFIFO | 0600, 0), EEXIST);
	EXPECT(S4(SYS_newfstatat, AT_FDCWD, "p", &st, AT_SYMLINK_NOFOLLOW), 0);
	CHECK(S_ISFIFO(st.st_mode));
	return CONF_PASS;
}

/*
 * lseek(2) on eventfd, timerfd and epoll fds: Linux gives them
 * noop_llseek, which leaves the position at 0. IR0 treated the objects as
 * regular files and wrote the offset into them.
 */
int fs_lseek_anon_fds_noop(void)
{
	struct { long sec; long nsec; } ts[2] = { { 0, 0 }, { 0, 0 } };
	unsigned long v = 3;
	long fds[3];
	int i;

	OK(fds[0], S2(SYS_eventfd2, 0, 0));
	OK(fds[1], S2(SYS_timerfd_create, CLOCK_MONOTONIC, 0));
	OK(fds[2], S1(SYS_epoll_create1, 0));
	for (i = 0; i < 3; i++)
	{
		EXPECT(S3(SYS_lseek, fds[i], 5, SEEK_SET), 0);
		EXPECT(S3(SYS_lseek, fds[i], 7, SEEK_CUR), 0);
		EXPECT(S3(SYS_lseek, fds[i], 0, SEEK_END), 0);
	}
	EXPECT(S3(SYS_write, fds[0], &v, sizeof(v)), (long)sizeof(v));
	v = 0;
	EXPECT(S3(SYS_read, fds[0], &v, sizeof(v)), (long)sizeof(v));
	EXPECT(v, 3);
	EXPECT(S4(SYS_timerfd_gettime, fds[1], ts, 0, 0), 0);
	return CONF_PASS;
}

/* lseek(2) on a memfd moves the position like on a regular file. */
int fs_lseek_memfd_like_file(void)
{
	long fd;

	OK(fd, S2(SYS_memfd_create, "m", MFD_CLOEXEC));
	EXPECT(S2(SYS_ftruncate, fd, 100), 0);
	EXPECT(S3(SYS_lseek, fd, 10, SEEK_SET), 10);
	EXPECT(S3(SYS_lseek, fd, 5, SEEK_CUR), 15);
	EXPECT(S3(SYS_lseek, fd, -1, SEEK_END), 99);
	EXPECT(S3(SYS_lseek, fd, 0, SEEK_CUR), 99);
	EXPECT_ERR(S3(SYS_lseek, fd, -200, SEEK_CUR), EINVAL);
	return CONF_PASS;
}

#define MANY_FILES 70

/* getdents64(2) returns every entry of a large directory across calls. */
int fs_getdents64_lists_all_entries(void)
{
	unsigned char seen[MANY_FILES];
	char name[16];
	char buf[512];
	long fd;
	long n;
	long off;
	int count = 0;
	int i;

	EXPECT(S3(SYS_mkdirat, AT_FDCWD, "d", 0755), 0);
	for (i = 0; i < MANY_FILES; i++)
	{
		name[0] = '\0';
		rt_cat(name, sizeof(name), "d/f");
		rt_catn(name, sizeof(name), i);
		EXPECT(make_file(name, 0), 0);
		seen[i] = 0;
	}
	OK(fd, S4(SYS_openat, AT_FDCWD, "d", O_RDONLY | O_DIRECTORY, 0));
	while ((n = S3(SYS_getdents64, fd, buf, sizeof(buf))) > 0)
	{
		for (off = 0; off < n;)
		{
			struct dirent *d = (struct dirent *)(buf + off);

			if (d->d_reclen == 0)
				break;
			if (d->d_name[0] == 'f')
			{
				i = (int)rt_atol(d->d_name + 1);
				CHECK(i >= 0 && i < MANY_FILES && !seen[i]);
				seen[i] = 1;
				count++;
			}
			off += d->d_reclen;
		}
	}
	EXPECT(n, 0);
	EXPECT(count, MANY_FILES);
	return CONF_PASS;
}

/* pread64(2) / pwrite64(2) use the given offset and leave the position. */
int fs_pread_pwrite_at_offset(void)
{
	char buf[8];
	long fd;

	OK(fd, conf_creat("f", 0));
	EXPECT(S4(SYS_pwrite64, fd, "abcd", 4, 20), 4);
	EXPECT(S3(SYS_lseek, fd, 0, SEEK_CUR), 0);
	rt_fill(buf, 'x', sizeof(buf));
	EXPECT(S4(SYS_pread64, fd, buf, 4, 10), 4);
	CHECK(buf[0] == 0 && buf[3] == 0);
	EXPECT(S4(SYS_pread64, fd, buf, sizeof(buf), 20), 4);
	CHECK(rt_memeq(buf, "abcd", 4));
	EXPECT(S4(SYS_pread64, fd, buf, 4, 100), 0);
	EXPECT_ERR(S4(SYS_pread64, fd, buf, 1, -1), EINVAL);
	return CONF_PASS;
}

/* pread64(2) on a pipe is ESPIPE. */
int fs_pread_pipe_espipe(void)
{
	char buf[4];
	int p[2];

	EXPECT(S2(SYS_pipe2, p, 0), 0);
	EXPECT_ERR(S4(SYS_pread64, p[0], buf, 1, 0), ESPIPE);
	EXPECT_ERR(S4(SYS_pwrite64, p[1], "x", 1, 0), ESPIPE);
	return CONF_PASS;
}

/* ftruncate(2) grows with zeros and shrinks; a read-only fd is EINVAL. */
int fs_ftruncate_resizes(void)
{
	struct stat st;
	char buf[4];
	long fd;
	long ro;

	OK(fd, conf_creat("f", "hello"));
	EXPECT(S2(SYS_ftruncate, fd, 64), 0);
	EXPECT(S2(SYS_fstat, fd, &st), 0);
	EXPECT(st.st_size, 64);
	rt_fill(buf, 'x', sizeof(buf));
	EXPECT(S4(SYS_pread64, fd, buf, sizeof(buf), 32), (long)sizeof(buf));
	CHECK(buf[0] == 0 && buf[3] == 0);
	EXPECT(S2(SYS_ftruncate, fd, 2), 0);
	EXPECT(S2(SYS_fstat, fd, &st), 0);
	EXPECT(st.st_size, 2);
	EXPECT_ERR(S2(SYS_ftruncate, fd, -1), EINVAL);
	OK(ro, S4(SYS_openat, AT_FDCWD, "f", O_RDONLY, 0));
	EXPECT_ERR(S2(SYS_ftruncate, ro, 0), EINVAL);
	return CONF_PASS;
}

/* linkat(2) adds a name for the same file; st_nlink counts both. */
int fs_linkat_hardlink(void)
{
	struct stat st;
	char buf[8];

	EXPECT(make_file("a", "data"), 0);
	EXPECT(S5(SYS_linkat, AT_FDCWD, "a", AT_FDCWD, "b", 0), 0);
	EXPECT(S4(SYS_newfstatat, AT_FDCWD, "b", &st, 0), 0);
	EXPECT(st.st_nlink, 2);
	EXPECT_ERR(S5(SYS_linkat, AT_FDCWD, "a", AT_FDCWD, "b", 0), EEXIST);
	EXPECT_ERR(S5(SYS_linkat, AT_FDCWD, "a", AT_FDCWD, "c", 0x8000), EINVAL);
	EXPECT(S3(SYS_unlinkat, AT_FDCWD, "a", 0), 0);
	EXPECT(read_file("b", buf, sizeof(buf)), 4);
	CHECK(rt_memeq(buf, "data", 4));
	return CONF_PASS;
}

/*
 * statx(2) fills struct statx: stx_mask at 0, stx_nlink at 16, stx_mode
 * at 28, stx_size at 40.
 */
int fs_statx_basic(void)
{
	unsigned char sx[256];
	unsigned long size;
	unsigned int mask;
	unsigned int nlink;
	unsigned short mode;

	EXPECT(make_file("f", "hello"), 0);
	EXPECT(S5(SYS_statx, AT_FDCWD, "f", 0, 0x7ffU, sx), 0);
	rt_copy(&mask, sx, sizeof(mask));
	rt_copy(&nlink, sx + 16, sizeof(nlink));
	rt_copy(&mode, sx + 28, sizeof(mode));
	rt_copy(&size, sx + 40, sizeof(size));
	CHECK((mask & 0x7ffU) == 0x7ffU);
	EXPECT(nlink, 1);
	CHECK(S_ISREG(mode));
	EXPECT((long)size, 5);
	EXPECT(S5(SYS_statx, AT_FDCWD, ".", 0, 0x7ffU, sx), 0);
	rt_copy(&mode, sx + 28, sizeof(mode));
	CHECK(S_ISDIR(mode));
	EXPECT_ERR(S5(SYS_statx, AT_FDCWD, "missing", 0, 0x7ffU, sx), ENOENT);
	EXPECT_ERR(S5(SYS_statx, AT_FDCWD, "f", 0x80000000U, 0x7ffU, sx), EINVAL);
	return CONF_PASS;
}

/*
 * DAC permission checks (fs/namei.c generic_permission()). The runner
 * forks each case, so a case may drop to an unprivileged user. Setting up
 * needs root and IDs 1000, 100 and 200 (a host user namespace that maps
 * only root has none of them), so a failed setup is TCONF; the IR0 runner
 * expects PASS for every listed case. Group IDs stay below 256: the MINIX
 * inode the guest /tmp lives on stores an 8-bit gid.
 */
#define DAC_UID 1000
#define DAC_GID 100
#define DAC_GROUP 200
#define DAC_X_OK 1
#define DAC_W_OK 2
#define DAC_R_OK 4

/* Become DAC_UID/DAC_GID with @group (if nonzero) as the only extra group. */
static int dac_become_user(unsigned int group)
{
	unsigned int groups[1] = { group };

	if (S2(SYS_setgroups, group ? 1 : 0, groups) != 0)
		return -1;
	if (S3(SYS_setresgid, DAC_GID, DAC_GID, DAC_GID) != 0)
		return -1;
	if (S3(SYS_setresuid, DAC_UID, DAC_UID, DAC_UID) != 0)
		return -1;
	return 0;
}

/* Create @path as root with @mode, owned by root:@gid. */
static int dac_make(const char *path, int is_dir, unsigned int mode,
		    unsigned int gid)
{
	long fd;

	if (is_dir)
	{
		if (S3(SYS_mkdirat, AT_FDCWD, path, 0700) != 0)
			return -1;
	}
	else
	{
		fd = conf_creat(path, "data");
		if (fd < 0 || S1(SYS_close, fd) != 0)
			return -1;
	}
	if (S5(SYS_fchownat, AT_FDCWD, path, 0, gid, 0) != 0)
		return -1;
	return S4(SYS_fchmodat, AT_FDCWD, path, mode, 0) == 0 ? 0 : -1;
}

/*
 * CAP_DAC_OVERRIDE: root reads and writes anything and searches any
 * directory, but executes a non-directory only when it has an exec bit.
 */
int fs_access_root_exec_needs_x_bit(void)
{
	if (S0(SYS_geteuid) != 0 || dac_make("f", 0, 0644, 0) != 0)
		return CONF_TCONF;
	EXPECT_ERR(S4(SYS_faccessat, AT_FDCWD, "f", DAC_X_OK, 0), EACCES);
	EXPECT(S4(SYS_fchmodat, AT_FDCWD, "f", 0000, 0), 0);
	EXPECT(S4(SYS_faccessat, AT_FDCWD, "f", DAC_R_OK | DAC_W_OK, 0), 0);
	EXPECT(S4(SYS_fchmodat, AT_FDCWD, "f", 0001, 0), 0);
	EXPECT(S4(SYS_faccessat, AT_FDCWD, "f", DAC_X_OK, 0), 0);
	EXPECT(dac_make("d", 1, 0000, 0), 0);
	EXPECT(S4(SYS_faccessat, AT_FDCWD, "d", DAC_X_OK, 0), 0);
	return CONF_PASS;
}

/*
 * acl_permission_check(): a supplementary group that owns the file selects
 * the group bits, even when they deny what the other bits would allow.
 */
int fs_supplementary_group_selects_group_bits(void)
{
	if (S0(SYS_geteuid) != 0 || dac_make("f", 0, 0604, DAC_GROUP) != 0 ||
	    dac_become_user(DAC_GROUP) != 0)
		return CONF_TCONF;
	EXPECT_ERR(S4(SYS_openat, AT_FDCWD, "f", O_RDONLY, 0), EACCES);
	EXPECT_ERR(S4(SYS_faccessat, AT_FDCWD, "f", DAC_R_OK, 0), EACCES);
	return CONF_PASS;
}

/* A supplementary group grants search on a directory its group owns. */
int fs_chdir_supplementary_group_search(void)
{
	if (S0(SYS_geteuid) != 0 || dac_make("d", 1, 0710, DAC_GROUP) != 0 ||
	    dac_become_user(DAC_GROUP) != 0)
		return CONF_TCONF;
	EXPECT(S1(SYS_chdir, "d"), 0);
	return CONF_PASS;
}

/*
 * The owner gets the owner bits only, even when group and other would
 * allow more; denied access is EACCES, while chmod by a non-owner is EPERM.
 */
int fs_dac_owner_bits_and_eperm(void)
{
	if (S0(SYS_geteuid) != 0 || dac_make("mine", 0, 0077, DAC_GID) != 0 ||
	    S5(SYS_fchownat, AT_FDCWD, "mine", DAC_UID, DAC_GID, 0) != 0 ||
	    dac_make("theirs", 0, 0666, 0) != 0 || dac_become_user(0) != 0)
		return CONF_TCONF;
	EXPECT_ERR(S4(SYS_openat, AT_FDCWD, "mine", O_RDONLY, 0), EACCES);
	EXPECT(S4(SYS_fchmodat, AT_FDCWD, "mine", 0600, 0), 0);
	EXPECT_ERR(S4(SYS_fchmodat, AT_FDCWD, "theirs", 0600, 0), EPERM);
	return CONF_PASS;
}

/*
 * setgroups(0, NULL) empties the supplementary groups (kernel/groups.c),
 * so root that drops to a user keeps no group 0 and cannot read a 0640
 * root:root file.
 */
int fs_setgroups_empty_drops_root_group(void)
{
	if (S0(SYS_geteuid) != 0 || dac_make("f", 0, 0640, 0) != 0 ||
	    dac_become_user(0) != 0)
		return CONF_TCONF;
	EXPECT_ERR(S4(SYS_openat, AT_FDCWD, "f", O_RDONLY, 0), EACCES);
	EXPECT(S2(SYS_getgroups, 0, 0), 0);
	return CONF_PASS;
}

/* Hand @path to DAC_UID:DAC_GID. */
static int dac_give(const char *path)
{
	return S5(SYS_fchownat, AT_FDCWD, path, DAC_UID, DAC_GID, 0) == 0 ?
		0 : -1;
}

/*
 * may_delete(): unlink and rmdir need write and search on the parent
 * directory, never write on the victim. A writable file in a directory the
 * user cannot write stays, a read-only file in the user's own directory
 * goes. The directory check comes before the type check (EACCES, not
 * EISDIR) and covers a FIFO too.
 */
int fs_unlink_needs_parent_write(void)
{
	if (S0(SYS_geteuid) != 0 || dac_make("ro", 1, 0755, 0) != 0 ||
	    dac_make("ro/f", 0, 0666, 0) != 0 ||
	    dac_make("ro/d", 1, 0777, 0) != 0 ||
	    S4(SYS_mknodat, AT_FDCWD, "ro/p", S_IFIFO | 0666, 0) != 0 ||
	    dac_make("own", 1, 0755, DAC_GID) != 0 || dac_give("own") != 0 ||
	    dac_make("own/f", 0, 0444, 0) != 0 ||
	    dac_make("own/d", 1, 0555, 0) != 0 || dac_become_user(0) != 0)
		return CONF_TCONF;
	EXPECT_ERR(S3(SYS_unlinkat, AT_FDCWD, "ro/f", 0), EACCES);
	EXPECT_ERR(S3(SYS_unlinkat, AT_FDCWD, "ro/p", 0), EACCES);
	EXPECT_ERR(S3(SYS_unlinkat, AT_FDCWD, "ro/d", 0), EACCES);
	EXPECT_ERR(S3(SYS_unlinkat, AT_FDCWD, "ro/d", AT_REMOVEDIR), EACCES);
	EXPECT(S3(SYS_unlinkat, AT_FDCWD, "own/f", 0), 0);
	EXPECT(S3(SYS_unlinkat, AT_FDCWD, "own/d", AT_REMOVEDIR), 0);
	return CONF_PASS;
}

/*
 * check_sticky(): in a sticky (+t) directory only the owner of the victim,
 * the owner of the directory or root removes a name, write permission on
 * the directory notwithstanding. The denial is EPERM, not EACCES.
 */
int fs_sticky_dir_restricts_delete(void)
{
	if (S0(SYS_geteuid) != 0 || dac_make("t", 1, 01777, 0) != 0 ||
	    dac_make("t/f", 0, 0666, 0) != 0 ||
	    dac_make("t/d", 1, 0777, 0) != 0 ||
	    dac_make("t/mine", 0, 0644, DAC_GID) != 0 ||
	    dac_give("t/mine") != 0 ||
	    dac_make("ut", 1, 01777, DAC_GID) != 0 || dac_give("ut") != 0 ||
	    dac_make("ut/f", 0, 0644, 0) != 0 || dac_become_user(0) != 0)
		return CONF_TCONF;
	EXPECT_ERR(S3(SYS_unlinkat, AT_FDCWD, "t/f", 0), EPERM);
	EXPECT_ERR(S3(SYS_unlinkat, AT_FDCWD, "t/d", AT_REMOVEDIR), EPERM);
	EXPECT(S3(SYS_unlinkat, AT_FDCWD, "t/mine", 0), 0);
	EXPECT(S3(SYS_unlinkat, AT_FDCWD, "ut/f", 0), 0);
	return CONF_PASS;
}

/*
 * may_create(): a new name (mkdir, mknod, link, symlink, open with
 * O_CREAT) needs write and search on the parent; write without search is
 * not enough. An existing name is EEXIST before the directory check, and
 * O_CREAT on an existing file does not look at the directory.
 */
int fs_create_needs_parent_write_search(void)
{
	long fd;

	if (S0(SYS_geteuid) != 0 || dac_make("ro", 1, 0755, 0) != 0 ||
	    dac_make("ro/mine", 0, 0644, DAC_GID) != 0 ||
	    dac_give("ro/mine") != 0 ||
	    dac_make("wo", 1, 0200, DAC_GID) != 0 || dac_give("wo") != 0 ||
	    dac_make("f", 0, 0644, DAC_GID) != 0 || dac_give("f") != 0 ||
	    dac_become_user(0) != 0)
		return CONF_TCONF;
	EXPECT_ERR(S3(SYS_mkdirat, AT_FDCWD, "wo/d", 0755), EACCES);
	EXPECT_ERR(S3(SYS_mkdirat, AT_FDCWD, "ro", 0755), EEXIST);
	EXPECT_ERR(S3(SYS_mkdirat, AT_FDCWD, "ro/d", 0755), EACCES);
	EXPECT_ERR(S4(SYS_mknodat, AT_FDCWD, "ro/p", S_IFIFO | 0644, 0),
		   EACCES);
	EXPECT_ERR(S3(SYS_symlinkat, "f", AT_FDCWD, "ro/l"), EACCES);
	EXPECT_ERR(S5(SYS_linkat, AT_FDCWD, "f", AT_FDCWD, "wo/l", 0), EACCES);
	EXPECT_ERR(S5(SYS_linkat, AT_FDCWD, "f", AT_FDCWD, "ro/l", 0), EACCES);
	EXPECT_ERR(S4(SYS_openat, AT_FDCWD, "ro/c", O_WRONLY | O_CREAT, 0644),
		   EACCES);
	OK(fd, S4(SYS_openat, AT_FDCWD, "ro/mine", O_WRONLY | O_CREAT, 0644));
	EXPECT(S1(SYS_close, fd), 0);
	return CONF_PASS;
}

/*
 * rename(2) deletes a name in the old directory and creates one (or
 * replaces one) in the new: both directories need write and search, the
 * file itself needs nothing, a sticky directory keeps names the user does
 * not own (EPERM), and a directory moved to another parent needs write on
 * itself to update "..".
 */
int fs_rename_needs_parent_write(void)
{
	if (S0(SYS_geteuid) != 0 || dac_make("ro", 1, 0755, 0) != 0 ||
	    dac_make("ro/f", 0, 0666, 0) != 0 ||
	    dac_make("own", 1, 0755, DAC_GID) != 0 || dac_give("own") != 0 ||
	    dac_make("own/f", 0, 0444, 0) != 0 ||
	    dac_make("own/d", 1, 0755, 0) != 0 ||
	    dac_make("own2", 1, 0755, DAC_GID) != 0 || dac_give("own2") != 0 ||
	    dac_make("t", 1, 01777, 0) != 0 ||
	    dac_make("t/f", 0, 0666, 0) != 0 || dac_become_user(0) != 0)
		return CONF_TCONF;
	EXPECT_ERR(S4(SYS_renameat, AT_FDCWD, "ro/f", AT_FDCWD, "own/x"),
		   EACCES);
	EXPECT(S4(SYS_renameat, AT_FDCWD, "own/f", AT_FDCWD, "own/g"), 0);
	EXPECT_ERR(S4(SYS_renameat, AT_FDCWD, "own/g", AT_FDCWD, "ro/g"),
		   EACCES);
	EXPECT_ERR(S4(SYS_renameat, AT_FDCWD, "t/f", AT_FDCWD, "own/t"),
		   EPERM);
	EXPECT_ERR(S4(SYS_renameat, AT_FDCWD, "own/g", AT_FDCWD, "t/f"),
		   EPERM);
	EXPECT_ERR(S4(SYS_renameat, AT_FDCWD, "own/d", AT_FDCWD, "own2/d"),
		   EACCES);
	return CONF_PASS;
}

/*
 * opendir needs read on the directory, not search: a 0711 directory does
 * not open for listing, a 0744 one opens and lists its entries, while the
 * files in it stay out of reach without search.
 */
int fs_opendir_needs_read(void)
{
	char buf[512];
	long fd;

	if (S0(SYS_geteuid) != 0 || dac_make("x", 1, 0711, 0) != 0 ||
	    dac_make("r", 1, 0744, 0) != 0 ||
	    dac_make("r/f", 0, 0644, 0) != 0 || dac_become_user(0) != 0)
		return CONF_TCONF;
	EXPECT_ERR(S4(SYS_openat, AT_FDCWD, "x", O_RDONLY | O_DIRECTORY, 0),
		   EACCES);
	OK(fd, S4(SYS_openat, AT_FDCWD, "r", O_RDONLY | O_DIRECTORY, 0));
	CHECK(S3(SYS_getdents64, fd, buf, sizeof(buf)) > 0);
	EXPECT(S1(SYS_close, fd), 0);
	EXPECT_ERR(S4(SYS_openat, AT_FDCWD, "r/f", O_RDONLY, 0), EACCES);
	return CONF_PASS;
}

/*
 * fs/namei.c path_openat(): a name means the same thing wherever it is.
 * Opening a missing file without O_CREAT is ENOENT and O_CREAT makes a
 * regular file, even under a "supervise" directory; only mkfifo(3)/mknod(2)
 * make a FIFO.
 */
int fs_open_names_no_special_paths(void)
{
	struct stat st;
	long fd;

	EXPECT(S3(SYS_mkdirat, AT_FDCWD, "sv", 0755), 0);
	EXPECT(S3(SYS_mkdirat, AT_FDCWD, "sv/supervise", 0755), 0);
	EXPECT_ERR(S4(SYS_openat, AT_FDCWD, "sv/supervise/control",
		      O_RDONLY | O_NONBLOCK, 0), ENOENT);
	OK(fd, S4(SYS_openat, AT_FDCWD, "sv/supervise/ok",
		  O_WRONLY | O_CREAT | O_NONBLOCK, 0600));
	EXPECT(S2(SYS_fstat, fd, &st), 0);
	CHECK(S_ISREG(st.st_mode));
	EXPECT(S1(SYS_close, fd), 0);
	return CONF_PASS;
}

/* Numeric field @want of a /proc/<pid>/stat line; fields after ')' start at 3. */
static int stat_field(const char *buf, int want, unsigned long long *out)
{
	const char *p = buf;
	const char *last = 0;
	int field = 3;

	for (; *p; p++)
		if (*p == ')')
			last = p;
	if (!last)
		return -1;
	for (p = last + 1; *p; field++)
	{
		while (*p == ' ' || *p == '\n')
			p++;
		if (field == want)
		{
			*out = 0;
			if (*p < '0' || *p > '9')
				return -1;
			while (*p >= '0' && *p <= '9')
				*out = *out * 10ULL + (unsigned long long)(*p++ - '0');
			return 0;
		}
		while (*p && *p != ' ' && *p != '\n')
			p++;
	}
	return -1;
}

/*
 * proc(5): /proc/<pid>/stat has flags, minflt, cminflt, majflt and cmajflt
 * as fields 9-13, then utime (14) and stime (15). IR0 printed four fields
 * there, so utime landed in cmajflt and stime in utime.
 */
int fs_proc_stat_utime_field(void)
{
	struct timespec ts;
	char buf[512];
	unsigned long long cmajflt;
	unsigned long long utime;
	volatile unsigned long spin = 0;
	long start;
	long now;
	long n;

	EXPECT(S2(SYS_clock_gettime, CLOCK_MONOTONIC, &ts), 0);
	start = ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
	do
	{
		unsigned long i;

		for (i = 0; i < (1UL << 20); i++)
			spin++;
		EXPECT(S2(SYS_clock_gettime, CLOCK_MONOTONIC, &ts), 0);
		now = ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
	} while (now - start < 200L);

	OK(n, read_file("/proc/self/stat", buf, (long)sizeof(buf) - 1));
	buf[n] = 0;
	CHECK(stat_field(buf, 13, &cmajflt) == 0);
	CHECK(stat_field(buf, 14, &utime) == 0);
	CHECK(cmajflt == 0);
	CHECK(utime > 0);
	return CONF_PASS;
}
