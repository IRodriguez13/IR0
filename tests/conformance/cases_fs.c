/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: cases_fs.c
 * Description: Conformance cases for file I/O, fd operations, the stat
 *              family and directories. Expected results follow Linux
 *              (fs/open.c, fs/read_write.c, fs/namei.c, fs/stat.c,
 *              fs/fcntl.c) and the man-pages ERRORS sections.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "conf.h"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/uio.h>

static const char hello[] = "hello world";
#define HELLO_LEN 11L

int fs_open_creat_excl(void)
{
	long fd;

	OK(fd, S4(SYS_openat, AT_FDCWD, "f", O_CREAT | O_EXCL | O_RDWR, 0644));
	EXPECT(S1(SYS_close, fd), 0);
	EXPECT_ERR(S4(SYS_openat, AT_FDCWD, "f", O_CREAT | O_EXCL | O_RDWR, 0644),
		   EEXIST);
	return CONF_PASS;
}

int fs_open_errors(void)
{
	long fd;

	EXPECT_ERR(S4(SYS_openat, AT_FDCWD, "missing", O_RDONLY, 0), ENOENT);
	EXPECT_ERR(S4(SYS_openat, AT_FDCWD, "", O_RDONLY, 0), ENOENT);
	OK(fd, conf_creat("f", 0));
	EXPECT_ERR(S4(SYS_openat, AT_FDCWD, "f/x", O_RDONLY, 0), ENOTDIR);
	EXPECT_ERR(S4(SYS_openat, AT_FDCWD, "f", O_RDONLY | O_DIRECTORY, 0),
		   ENOTDIR);
	EXPECT_ERR(S4(SYS_openat, AT_FDCWD, ".", O_WRONLY, 0), EISDIR);
	EXPECT_ERR(S4(SYS_openat, AT_FDCWD, "missing/x", O_CREAT | O_RDWR, 0644),
		   ENOENT);
	EXPECT_ERR(S4(SYS_openat, -1, "rel", O_RDONLY, 0), EBADF);
	return CONF_PASS;
}

int fs_write_read_roundtrip(void)
{
	char buf[32];
	long fd;

	OK(fd, conf_creat("f", hello));
	EXPECT(S3(SYS_lseek, fd, 0, SEEK_SET), 0);
	EXPECT(S3(SYS_read, fd, buf, sizeof(buf)), HELLO_LEN);
	CHECK(rt_memeq(buf, hello, HELLO_LEN));
	EXPECT(S3(SYS_read, fd, buf, sizeof(buf)), 0);
	return CONF_PASS;
}

int fs_lseek_whence(void)
{
	long fd;
	long r;
	int p[2];

	OK(fd, conf_creat("f", hello));
	EXPECT(S3(SYS_lseek, fd, 0, SEEK_END), HELLO_LEN);
	EXPECT(S3(SYS_lseek, fd, -5, SEEK_CUR), HELLO_LEN - 5);
	EXPECT(S3(SYS_lseek, fd, 100, SEEK_SET), 100);
	EXPECT_ERR(S3(SYS_lseek, fd, -1, SEEK_SET), EINVAL);
	EXPECT_ERR(S3(SYS_lseek, fd, 0, 99), EINVAL);
	EXPECT_ERR(S3(SYS_lseek, -1, 0, SEEK_SET), EBADF);
	OK(r, S2(SYS_pipe2, p, 0));
	EXPECT_ERR(S3(SYS_lseek, p[0], 0, SEEK_SET), ESPIPE);
	return CONF_PASS;
}

int fs_pread_pwrite(void)
{
	struct stat st;
	char buf[16];
	long fd;

	OK(fd, conf_creat("f", 0));
	EXPECT(S4(SYS_pwrite64, fd, hello, HELLO_LEN, 100), HELLO_LEN);
	EXPECT(S3(SYS_lseek, fd, 0, SEEK_CUR), 0);
	EXPECT(S2(SYS_fstat, fd, &st), 0);
	EXPECT(st.st_size, 100 + HELLO_LEN);
	rt_fill(buf, 'x', sizeof(buf));
	EXPECT(S4(SYS_pread64, fd, buf, 4, 50), 4);
	CHECK(buf[0] == 0 && buf[1] == 0 && buf[2] == 0 && buf[3] == 0);
	EXPECT(S4(SYS_pread64, fd, buf, HELLO_LEN, 100), HELLO_LEN);
	CHECK(rt_memeq(buf, hello, HELLO_LEN));
	EXPECT_ERR(S4(SYS_pread64, fd, buf, 1, -1), EINVAL);
	return CONF_PASS;
}

int fs_o_append(void)
{
	struct stat st;
	long fd;

	OK(fd, conf_creat("f", hello));
	EXPECT(S1(SYS_close, fd), 0);
	OK(fd, S4(SYS_openat, AT_FDCWD, "f", O_WRONLY | O_APPEND, 0));
	EXPECT(S3(SYS_lseek, fd, 0, SEEK_SET), 0);
	EXPECT(S3(SYS_write, fd, "!", 1), 1);
	EXPECT(S3(SYS_lseek, fd, 0, SEEK_CUR), HELLO_LEN + 1);
	EXPECT(S2(SYS_fstat, fd, &st), 0);
	EXPECT(st.st_size, HELLO_LEN + 1);
	return CONF_PASS;
}

int fs_o_trunc(void)
{
	struct stat st;
	long fd;

	OK(fd, conf_creat("f", hello));
	EXPECT(S1(SYS_close, fd), 0);
	OK(fd, S4(SYS_openat, AT_FDCWD, "f", O_RDWR | O_TRUNC, 0));
	EXPECT(S2(SYS_fstat, fd, &st), 0);
	EXPECT(st.st_size, 0);
	return CONF_PASS;
}

int fs_ftruncate(void)
{
	struct stat st;
	char buf[8];
	long fd;
	long ro;

	OK(fd, conf_creat("f", hello));
	EXPECT(S2(SYS_ftruncate, fd, 8192), 0);
	EXPECT(S2(SYS_fstat, fd, &st), 0);
	EXPECT(st.st_size, 8192);
	EXPECT(S4(SYS_pread64, fd, buf, sizeof(buf), 4096), (long)sizeof(buf));
	CHECK(buf[0] == 0 && buf[7] == 0);
	EXPECT(S2(SYS_ftruncate, fd, 3), 0);
	EXPECT(S2(SYS_fstat, fd, &st), 0);
	EXPECT(st.st_size, 3);
	EXPECT_ERR(S2(SYS_ftruncate, fd, -1), EINVAL);
	OK(ro, S4(SYS_openat, AT_FDCWD, "f", O_RDONLY, 0));
	EXPECT_ERR(S2(SYS_ftruncate, ro, 0), EINVAL);
	return CONF_PASS;
}

int fs_access_mode_ebadf(void)
{
	char buf[4];
	long wo;
	long ro;

	OK(wo, conf_creat("f", hello));
	EXPECT(S1(SYS_close, wo), 0);
	OK(wo, S4(SYS_openat, AT_FDCWD, "f", O_WRONLY, 0));
	OK(ro, S4(SYS_openat, AT_FDCWD, "f", O_RDONLY, 0));
	EXPECT_ERR(S3(SYS_read, wo, buf, sizeof(buf)), EBADF);
	EXPECT_ERR(S3(SYS_write, ro, "x", 1), EBADF);
	EXPECT_ERR(S3(SYS_read, -1, buf, sizeof(buf)), EBADF);
	EXPECT_ERR(S3(SYS_write, 4000, "x", 1), EBADF);
	return CONF_PASS;
}

int fs_readv_writev(void)
{
	struct iovec iov[2];
	char a[6];
	char b[5];
	long fd;

	OK(fd, conf_creat("f", 0));
	iov[0].iov_base = (void *)hello;
	iov[0].iov_len = 6;
	iov[1].iov_base = (void *)(hello + 6);
	iov[1].iov_len = 5;
	EXPECT(S3(SYS_writev, fd, iov, 2), HELLO_LEN);
	EXPECT(S3(SYS_lseek, fd, 0, SEEK_SET), 0);
	iov[0].iov_base = a;
	iov[1].iov_base = b;
	EXPECT(S3(SYS_readv, fd, iov, 2), HELLO_LEN);
	CHECK(rt_memeq(a, hello, 6) && rt_memeq(b, hello + 6, 5));
	EXPECT_ERR(S3(SYS_writev, fd, iov, -1), EINVAL);
	return CONF_PASS;
}

int fs_bad_buffer_efault(void)
{
	long fd;

	OK(fd, conf_creat("f", hello));
	EXPECT(S3(SYS_lseek, fd, 0, SEEK_SET), 0);
	EXPECT_ERR(S3(SYS_read, fd, 0, 4), EFAULT);
	EXPECT_ERR(S3(SYS_write, fd, 0, 4), EFAULT);
	EXPECT_ERR(S3(SYS_read, fd, RT_KERNEL_ADDR_A, 4), EFAULT);
	EXPECT_ERR(S4(SYS_openat, AT_FDCWD, 0, O_RDONLY, 0), EFAULT);
	EXPECT_ERR(S4(SYS_openat, AT_FDCWD, RT_KERNEL_ADDR_B, O_RDONLY, 0),
		   EFAULT);
	return CONF_PASS;
}

int fs_zero_length_io(void)
{
	long fd;

	OK(fd, conf_creat("f", hello));
	EXPECT(S3(SYS_write, fd, 0, 0), 0);
	EXPECT(S3(SYS_read, fd, 0, 0), 0);
	return CONF_PASS;
}

int fs_read_directory_eisdir(void)
{
	char buf[8];
	long fd;

	OK(fd, S4(SYS_openat, AT_FDCWD, ".", O_RDONLY | O_DIRECTORY, 0));
	EXPECT_ERR(S3(SYS_read, fd, buf, sizeof(buf)), EISDIR);
	return CONF_PASS;
}

int fs_unlink_open_file(void)
{
	struct stat st;
	char buf[16];
	long fd;

	OK(fd, conf_creat("f", hello));
	EXPECT(S3(SYS_unlinkat, AT_FDCWD, "f", 0), 0);
	EXPECT_ERR(S4(SYS_newfstatat, AT_FDCWD, "f", &st, 0), ENOENT);
	EXPECT(S4(SYS_pread64, fd, buf, HELLO_LEN, 0), HELLO_LEN);
	CHECK(rt_memeq(buf, hello, HELLO_LEN));
	EXPECT(S2(SYS_fstat, fd, &st), 0);
	EXPECT(st.st_nlink, 0);
	EXPECT_ERR(S3(SYS_unlinkat, AT_FDCWD, "f", 0), ENOENT);
	return CONF_PASS;
}

int fs_rename(void)
{
	struct stat st;
	char buf[16];
	long fd;

	OK(fd, conf_creat("a", hello));
	EXPECT(S1(SYS_close, fd), 0);
	OK(fd, conf_creat("b", "old"));
	EXPECT(S1(SYS_close, fd), 0);
	EXPECT(S4(SYS_renameat, AT_FDCWD, "a", AT_FDCWD, "b"), 0);
	EXPECT_ERR(S4(SYS_newfstatat, AT_FDCWD, "a", &st, 0), ENOENT);
	OK(fd, S4(SYS_openat, AT_FDCWD, "b", O_RDONLY, 0));
	EXPECT(S3(SYS_read, fd, buf, sizeof(buf)), HELLO_LEN);
	EXPECT_ERR(S4(SYS_renameat, AT_FDCWD, "a", AT_FDCWD, "c"), ENOENT);
	return CONF_PASS;
}

int fs_fsync(void)
{
	long fd;

	OK(fd, conf_creat("f", hello));
	EXPECT(S1(SYS_fsync, fd), 0);
	EXPECT(S1(SYS_fdatasync, fd), 0);
	EXPECT_ERR(S1(SYS_fsync, -1), EBADF);
	return CONF_PASS;
}

int fd_close_ebadf(void)
{
	long fd;

	EXPECT_ERR(S1(SYS_close, -1), EBADF);
	EXPECT_ERR(S1(SYS_close, 4000), EBADF);
	OK(fd, conf_creat("f", 0));
	EXPECT(S1(SYS_close, fd), 0);
	EXPECT_ERR(S1(SYS_close, fd), EBADF);
	return CONF_PASS;
}

int fd_dup_lowest(void)
{
	long a;
	long b;

	OK(a, conf_creat("a", 0));
	OK(b, conf_creat("b", 0));
	EXPECT(S1(SYS_close, a), 0);
	EXPECT(S1(SYS_dup, b), a);
	EXPECT_ERR(S1(SYS_dup, -1), EBADF);
	return CONF_PASS;
}

int fd_dup3(void)
{
	long fd;

	OK(fd, conf_creat("f", 0));
	EXPECT_ERR(S3(SYS_dup3, fd, fd, 0), EINVAL);
	/* Below every fd limit in use (IR0 RLIMIT_NOFILE is 64). */
	EXPECT(S3(SYS_dup3, fd, 40, O_CLOEXEC), 40);
	EXPECT(S2(SYS_fcntl, 40, F_GETFD), FD_CLOEXEC);
	EXPECT(S3(SYS_dup3, fd, 41, 0), 41);
	EXPECT(S2(SYS_fcntl, 41, F_GETFD), 0);
	EXPECT_ERR(S3(SYS_dup3, fd, 42, 0x7fff0000), EINVAL);
	EXPECT_ERR(S3(SYS_dup3, -1, 43, 0), EBADF);
	return CONF_PASS;
}

int fd_fcntl_dupfd(void)
{
	long fd;
	long d;

	OK(fd, conf_creat("f", 0));
	OK(d, S3(SYS_fcntl, fd, F_DUPFD, 20));
	CHECK(d >= 20);
	EXPECT(S2(SYS_fcntl, d, F_GETFD), 0);
	OK(d, S3(SYS_fcntl, fd, F_DUPFD_CLOEXEC, 30));
	CHECK(d >= 30);
	EXPECT(S2(SYS_fcntl, d, F_GETFD), FD_CLOEXEC);
	EXPECT(S3(SYS_fcntl, d, F_SETFD, 0), 0);
	EXPECT(S2(SYS_fcntl, d, F_GETFD), 0);
	EXPECT_ERR(S2(SYS_fcntl, -1, F_GETFD), EBADF);
	EXPECT_ERR(S2(SYS_fcntl, fd, 0x7fff), EINVAL);
	return CONF_PASS;
}

int fd_fcntl_flags(void)
{
	long fd;
	long fl;

	OK(fd, conf_creat("f", 0));
	OK(fl, S2(SYS_fcntl, fd, F_GETFL));
	EXPECT(fl & O_ACCMODE, O_RDWR);
	EXPECT(S3(SYS_fcntl, fd, F_SETFL, O_WRONLY | O_APPEND | O_NONBLOCK), 0);
	OK(fl, S2(SYS_fcntl, fd, F_GETFL));
	EXPECT(fl & O_ACCMODE, O_RDWR);
	EXPECT(fl & (O_APPEND | O_NONBLOCK), O_APPEND | O_NONBLOCK);
	return CONF_PASS;
}

int fd_dup_shares_offset(void)
{
	long fd;
	long d;

	OK(fd, conf_creat("f", 0));
	OK(d, S1(SYS_dup, fd));
	EXPECT(S3(SYS_write, fd, hello, HELLO_LEN), HELLO_LEN);
	EXPECT(S3(SYS_lseek, d, 0, SEEK_CUR), HELLO_LEN);
	EXPECT(S3(SYS_lseek, d, 2, SEEK_SET), 2);
	EXPECT(S3(SYS_lseek, fd, 0, SEEK_CUR), 2);
	return CONF_PASS;
}

int stat_fstat_regular(void)
{
	struct stat st;
	struct stat st2;
	long fd;

	(void)S1(SYS_umask, 0);
	OK(fd, S4(SYS_openat, AT_FDCWD, "f", O_CREAT | O_RDWR, 0640));
	EXPECT(S3(SYS_write, fd, hello, HELLO_LEN), HELLO_LEN);
	EXPECT(S2(SYS_fstat, fd, &st), 0);
	CHECK(S_ISREG(st.st_mode));
	EXPECT(st.st_mode & 07777, 0640);
	EXPECT(st.st_size, HELLO_LEN);
	EXPECT(st.st_nlink, 1);
	EXPECT(S4(SYS_newfstatat, AT_FDCWD, "f", &st2, 0), 0);
	CHECK(st.st_ino == st2.st_ino && st.st_dev == st2.st_dev);
	EXPECT(st2.st_size, HELLO_LEN);
	EXPECT_ERR(S2(SYS_fstat, -1, &st), EBADF);
	EXPECT_ERR(S2(SYS_fstat, fd, 0), EFAULT);
	return CONF_PASS;
}

int stat_umask(void)
{
	struct stat st;
	long fd;

	EXPECT(S1(SYS_umask, 027), 022);
	EXPECT(S1(SYS_umask, 027), 027);
	OK(fd, S4(SYS_openat, AT_FDCWD, "f", O_CREAT | O_RDWR, 0666));
	EXPECT(S2(SYS_fstat, fd, &st), 0);
	EXPECT(st.st_mode & 0777, 0640);
	EXPECT(S3(SYS_mkdirat, AT_FDCWD, "d", 0777), 0);
	EXPECT(S4(SYS_newfstatat, AT_FDCWD, "d", &st, 0), 0);
	EXPECT(st.st_mode & 0777, 0750);
	return CONF_PASS;
}

int stat_empty_path(void)
{
	struct stat st;
	long fd;

	OK(fd, conf_creat("f", hello));
	EXPECT_ERR(S4(SYS_newfstatat, AT_FDCWD, "", &st, 0), ENOENT);
	EXPECT(S4(SYS_newfstatat, fd, "", &st, AT_EMPTY_PATH), 0);
	EXPECT(st.st_size, HELLO_LEN);
	EXPECT_ERR(S4(SYS_newfstatat, AT_FDCWD, "missing", &st, 0), ENOENT);
	EXPECT_ERR(S4(SYS_newfstatat, AT_FDCWD, "f/x", &st, 0), ENOTDIR);
	return CONF_PASS;
}

int stat_symlink(void)
{
	struct stat st;
	char buf[32];
	long fd;

	OK(fd, conf_creat("target", hello));
	EXPECT(S3(SYS_symlinkat, "target", AT_FDCWD, "link"), 0);
	EXPECT(S4(SYS_newfstatat, AT_FDCWD, "link", &st, AT_SYMLINK_NOFOLLOW), 0);
	CHECK(S_ISLNK(st.st_mode));
	EXPECT(st.st_size, 6);
	EXPECT(S4(SYS_newfstatat, AT_FDCWD, "link", &st, 0), 0);
	CHECK(S_ISREG(st.st_mode));
	EXPECT(st.st_size, HELLO_LEN);
	rt_fill(buf, 'z', sizeof(buf));
	EXPECT(S4(SYS_readlinkat, AT_FDCWD, "link", buf, sizeof(buf)), 6);
	CHECK(rt_memeq(buf, "target", 6) && buf[6] == 'z');
	EXPECT_ERR(S4(SYS_readlinkat, AT_FDCWD, "target", buf, sizeof(buf)),
		   EINVAL);
	EXPECT_ERR(S3(SYS_symlinkat, "x", AT_FDCWD, "link"), EEXIST);
	EXPECT_ERR(S4(SYS_openat, AT_FDCWD, "link", O_RDONLY | O_NOFOLLOW, 0),
		   ELOOP);
	return CONF_PASS;
}

int stat_hardlink(void)
{
	struct stat st;
	long fd;

	OK(fd, conf_creat("a", hello));
	EXPECT(S5(SYS_linkat, AT_FDCWD, "a", AT_FDCWD, "b", 0), 0);
	EXPECT(S2(SYS_fstat, fd, &st), 0);
	EXPECT(st.st_nlink, 2);
	EXPECT_ERR(S5(SYS_linkat, AT_FDCWD, "a", AT_FDCWD, "b", 0), EEXIST);
	EXPECT(S3(SYS_unlinkat, AT_FDCWD, "a", 0), 0);
	EXPECT(S4(SYS_newfstatat, AT_FDCWD, "b", &st, 0), 0);
	EXPECT(st.st_nlink, 1);
	EXPECT(st.st_size, HELLO_LEN);
	return CONF_PASS;
}

int stat_fifo_and_dir(void)
{
	struct stat st;
	int p[2];
	long r;

	OK(r, S2(SYS_pipe2, p, 0));
	EXPECT(S2(SYS_fstat, p[0], &st), 0);
	CHECK(S_ISFIFO(st.st_mode));
	EXPECT(S3(SYS_mkdirat, AT_FDCWD, "d", 0755), 0);
	EXPECT(S4(SYS_newfstatat, AT_FDCWD, "d", &st, 0), 0);
	CHECK(S_ISDIR(st.st_mode));
	return CONF_PASS;
}

int stat_statx(void)
{
	/* struct statx: stx_mask at 0, stx_mode at 28, stx_size at 40. */
	unsigned char sx[256];
	unsigned long size;
	unsigned short mode;
	long fd;

	OK(fd, conf_creat("f", hello));
	EXPECT(S5(SYS_statx, AT_FDCWD, "f", 0, 0x7ffU, sx), 0);
	rt_copy(&size, sx + 40, sizeof(size));
	rt_copy(&mode, sx + 28, sizeof(mode));
	EXPECT((long)size, HELLO_LEN);
	CHECK(S_ISREG(mode));
	EXPECT_ERR(S5(SYS_statx, AT_FDCWD, "missing", 0, 0x7ffU, sx), ENOENT);
	return CONF_PASS;
}

int stat_faccessat(void)
{
	long fd;

	OK(fd, conf_creat("f", 0));
	EXPECT(S3(SYS_faccessat, AT_FDCWD, "f", F_OK), 0);
	EXPECT(S3(SYS_faccessat, AT_FDCWD, "f", R_OK | W_OK), 0);
	EXPECT_ERR(S3(SYS_faccessat, AT_FDCWD, "missing", F_OK), ENOENT);
	EXPECT_ERR(S3(SYS_faccessat, AT_FDCWD, "f", 0x40), EINVAL);
	return CONF_PASS;
}

int dir_mkdir_rmdir(void)
{
	long fd;

	EXPECT(S3(SYS_mkdirat, AT_FDCWD, "d", 0755), 0);
	EXPECT_ERR(S3(SYS_mkdirat, AT_FDCWD, "d", 0755), EEXIST);
	EXPECT_ERR(S3(SYS_mkdirat, AT_FDCWD, "missing/d", 0755), ENOENT);
	OK(fd, conf_creat("d/f", 0));
	EXPECT_ERR(S3(SYS_unlinkat, AT_FDCWD, "d", AT_REMOVEDIR), ENOTEMPTY);
	EXPECT_ERR(S3(SYS_unlinkat, AT_FDCWD, "d", 0), EISDIR);
	EXPECT_ERR(S3(SYS_unlinkat, AT_FDCWD, "d/f", AT_REMOVEDIR), ENOTDIR);
	EXPECT(S3(SYS_unlinkat, AT_FDCWD, "d/f", 0), 0);
	EXPECT(S3(SYS_unlinkat, AT_FDCWD, "d", AT_REMOVEDIR), 0);
	EXPECT_ERR(S3(SYS_unlinkat, AT_FDCWD, "d", AT_REMOVEDIR), ENOENT);
	return CONF_PASS;
}

int dir_getdents64(void)
{
	char buf[1024];
	unsigned int seen = 0;
	unsigned int entries = 0;
	long fd;
	long n;

	OK(fd, conf_creat("a", 0));
	OK(fd, conf_creat("b", 0));
	EXPECT(S3(SYS_mkdirat, AT_FDCWD, "c", 0755), 0);
	OK(fd, S4(SYS_openat, AT_FDCWD, ".", O_RDONLY | O_DIRECTORY, 0));
	while ((n = S3(SYS_getdents64, fd, buf, sizeof(buf))) > 0)
	{
		long off = 0;

		while (off < n)
		{
			struct dirent *d = (struct dirent *)(buf + off);

			CHECK(d->d_reclen != 0);
			entries++;
			if (rt_streq(d->d_name, "."))
				seen |= 1;
			else if (rt_streq(d->d_name, ".."))
				seen |= 2;
			else if (rt_streq(d->d_name, "a"))
				seen |= 4;
			else if (rt_streq(d->d_name, "b"))
				seen |= 8;
			else if (rt_streq(d->d_name, "c"))
			{
				seen |= 16;
				EXPECT(d->d_type, DT_DIR);
			}
			off += d->d_reclen;
		}
	}
	EXPECT(n, 0);
	EXPECT(seen, 31);
	EXPECT(entries, 5);
	EXPECT(S3(SYS_lseek, fd, 0, SEEK_SET), 0);
	EXPECT_ERR(S3(SYS_getdents64, fd, buf, 1), EINVAL);
	return CONF_PASS;
}

int dir_getdents64_not_dir(void)
{
	char buf[256];
	long fd;

	OK(fd, conf_creat("f", 0));
	EXPECT_ERR(S3(SYS_getdents64, fd, buf, sizeof(buf)), ENOTDIR);
	EXPECT_ERR(S3(SYS_getdents64, -1, buf, sizeof(buf)), EBADF);
	return CONF_PASS;
}

int dir_chdir_getcwd(void)
{
	char before[256];
	char after[256];
	long n;
	long m;
	long fd;

	OK(n, S2(SYS_getcwd, before, sizeof(before)));
	/* Linux returns the length including the terminating NUL. */
	EXPECT(n, (long)rt_strlen(before) + 1);
	CHECK(before[0] == '/');
	EXPECT(S3(SYS_mkdirat, AT_FDCWD, "sub", 0755), 0);
	EXPECT(S1(SYS_chdir, "sub"), 0);
	OK(m, S2(SYS_getcwd, after, sizeof(after)));
	EXPECT(m, n + 4);
	CHECK(rt_memeq(after, before, (size_t)n - 1));
	CHECK(rt_streq(after + n - 1, "/sub"));
	EXPECT_ERR(S2(SYS_getcwd, after, 1), ERANGE);
	EXPECT_ERR(S1(SYS_chdir, "missing"), ENOENT);
	OK(fd, conf_creat("f", 0));
	EXPECT_ERR(S1(SYS_chdir, "f"), ENOTDIR);
	OK(fd, S4(SYS_openat, AT_FDCWD, "..", O_RDONLY | O_DIRECTORY, 0));
	EXPECT(S1(SYS_fchdir, fd), 0);
	OK(m, S2(SYS_getcwd, after, sizeof(after)));
	CHECK(rt_streq(after, before));
	return CONF_PASS;
}

int dir_openat_dirfd(void)
{
	struct stat st;
	long dfd;
	long fd;

	EXPECT(S3(SYS_mkdirat, AT_FDCWD, "d", 0755), 0);
	OK(dfd, S4(SYS_openat, AT_FDCWD, "d", O_RDONLY | O_DIRECTORY, 0));
	OK(fd, S4(SYS_openat, dfd, "inner", O_CREAT | O_RDWR, 0644));
	EXPECT(S4(SYS_newfstatat, AT_FDCWD, "d/inner", &st, 0), 0);
	EXPECT(S3(SYS_mkdirat, dfd, "sub", 0755), 0);
	EXPECT(S4(SYS_newfstatat, dfd, "sub", &st, 0), 0);
	CHECK(S_ISDIR(st.st_mode));
	EXPECT_ERR(S4(SYS_openat, fd, "x", O_RDONLY, 0), ENOTDIR);
	return CONF_PASS;
}
