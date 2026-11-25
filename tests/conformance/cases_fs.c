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
#include <sys/mman.h>
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

/* A rename over an open file takes its name, not its data (Linux iput). */
int fs_rename_over_open_file(void)
{
	struct stat st;
	char buf[16];
	long fd;
	long fd2;

	OK(fd, conf_creat("a", hello));
	EXPECT(S1(SYS_close, fd), 0);
	OK(fd, conf_creat("b", "old"));
	EXPECT(S4(SYS_renameat, AT_FDCWD, "a", AT_FDCWD, "b"), 0);
	EXPECT(S4(SYS_pread64, fd, buf, sizeof(buf), 0), 3);
	CHECK(rt_memeq(buf, "old", 3));
	EXPECT(S2(SYS_fstat, fd, &st), 0);
	EXPECT(st.st_nlink, 0);
	EXPECT(S4(SYS_pwrite64, fd, "new", 3, 0), 3);
	OK(fd2, S4(SYS_openat, AT_FDCWD, "b", O_RDONLY, 0));
	EXPECT(S3(SYS_read, fd2, buf, sizeof(buf)), HELLO_LEN);
	CHECK(rt_memeq(buf, hello, HELLO_LEN));
	return CONF_PASS;
}

/* Two names of one file share its page cache (Linux address_space). */
int fs_link_shared_mapping(void)
{
	volatile char *pa;
	volatile char *pb;
	long fa;
	long fb;
	long va;

	OK(fa, conf_creat("a", hello));
	EXPECT(S5(SYS_linkat, AT_FDCWD, "a", AT_FDCWD, "b", 0), 0);
	OK(fb, S4(SYS_openat, AT_FDCWD, "b", O_RDWR, 0));
	OK(va, S6(SYS_mmap, 0, RT_PAGE, PROT_READ | PROT_WRITE, MAP_SHARED, fa,
		  0));
	pa = (volatile char *)va;
	OK(va, S6(SYS_mmap, 0, RT_PAGE, PROT_READ | PROT_WRITE, MAP_SHARED, fb,
		  0));
	pb = (volatile char *)va;
	CHECK(pa != pb);
	pa[0] = 'J';
	CHECK(pb[0] == 'J');
	pb[1] = 'E';
	CHECK(pa[1] == 'E');
	EXPECT(S2(SYS_munmap, (long)pa, RT_PAGE), 0);
	EXPECT(S2(SYS_munmap, (long)pb, RT_PAGE), 0);
	return CONF_PASS;
}

/* tmpfs keeps an unlinked open file and shares one cache per inode. */
int fs_tmpfs_inode_lifetime(void)
{
	struct stat st;
	volatile char *pa;
	volatile char *pb;
	char buf[16];
	long fd;
	long fb;
	long va;

	EXPECT(S3(SYS_mkdirat, AT_FDCWD, "t", 0755), 0);
	EXPECT(S5(SYS_mount, "none", "t", "tmpfs", 0, 0), 0);
	OK(fd, conf_creat("t/f", hello));
	EXPECT(S3(SYS_unlinkat, AT_FDCWD, "t/f", 0), 0);
	EXPECT(S4(SYS_pread64, fd, buf, HELLO_LEN, 0), HELLO_LEN);
	CHECK(rt_memeq(buf, hello, HELLO_LEN));
	EXPECT(S2(SYS_fstat, fd, &st), 0);
	EXPECT(st.st_nlink, 0);
	EXPECT(S1(SYS_close, fd), 0);

	OK(fd, conf_creat("t/a", hello));
	EXPECT(S5(SYS_linkat, AT_FDCWD, "t/a", AT_FDCWD, "t/b", 0), 0);
	OK(fb, S4(SYS_openat, AT_FDCWD, "t/b", O_RDWR, 0));
	OK(va, S6(SYS_mmap, 0, RT_PAGE, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
		  0));
	pa = (volatile char *)va;
	OK(va, S6(SYS_mmap, 0, RT_PAGE, PROT_READ | PROT_WRITE, MAP_SHARED, fb,
		  0));
	pb = (volatile char *)va;
	pa[0] = 'J';
	CHECK(pb[0] == 'J');
	EXPECT(S2(SYS_munmap, (long)pa, RT_PAGE), 0);
	EXPECT(S2(SYS_munmap, (long)pb, RT_PAGE), 0);
	EXPECT(S1(SYS_close, fd), 0);
	EXPECT(S1(SYS_close, fb), 0);
	EXPECT(S2(SYS_umount2, "t", 0), 0);
	return CONF_PASS;
}

/* A filesystem with an open file cannot be unmounted (Linux do_umount). */
int fs_umount_busy(void)
{
	long fd;

	EXPECT(S3(SYS_mkdirat, AT_FDCWD, "t", 0755), 0);
	EXPECT(S5(SYS_mount, "none", "t", "tmpfs", 0, 0), 0);
	OK(fd, conf_creat("t/f", hello));
	EXPECT_ERR(S2(SYS_umount2, "t", 0), EBUSY);
	EXPECT(S1(SYS_close, fd), 0);
	EXPECT(S2(SYS_umount2, "t", 0), 0);
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
	/* Below the soft RLIMIT_NOFILE (IR0 default is 1024). */
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

/*
 * Every path lookup needs search (x) permission on each directory it
 * walks, and that check comes before anything about the final name: a
 * caller without it learns neither that a name exists (EEXIST) nor that
 * it is missing (ENOENT). Each call below gets EACCES on Linux.
 */
#define NS_CALLS 16

static long ns_call(int i)
{
	struct timespec ts[2] = { { 0, 0 }, { 0, 0 } };
	char buf[256];
	struct stat st;

	switch (i)
	{
	case 0: return S4(SYS_newfstatat, AT_FDCWD, "ns/f", &st, 0);
	case 1: return S4(SYS_newfstatat, AT_FDCWD, "ns/missing", &st, 0);
	case 2: return S4(SYS_faccessat, AT_FDCWD, "ns/f", F_OK, 0);
	case 3: return S1(SYS_chdir, "ns/sub");
	case 4: return S4(SYS_readlinkat, AT_FDCWD, "ns/f", buf, sizeof(buf));
	case 5: return S4(SYS_fchmodat, AT_FDCWD, "ns/f", 0644, 0);
	case 6: return S5(SYS_fchownat, AT_FDCWD, "ns/f", -1, -1, 0);
	case 7: return S2(SYS_truncate, "ns/f", 0);
	case 8: return S4(SYS_utimensat, AT_FDCWD, "ns/f", ts, 0);
	case 9: return S2(SYS_statfs, "ns/f", buf);
	case 10: return S4(SYS_mknodat, AT_FDCWD, "ns/fifo", S_IFIFO | 0644, 0);
	case 11: return S3(SYS_mkdirat, AT_FDCWD, "ns/f", 0755);
	case 12: return S4(SYS_openat, AT_FDCWD, "ns/f",
			   O_CREAT | O_EXCL | O_WRONLY, 0644);
	case 13: return S3(SYS_unlinkat, AT_FDCWD, "ns/missing", 0);
	case 14: return S4(SYS_openat, AT_FDCWD, "ns/sub", O_RDONLY | O_DIRECTORY, 0);
	default: return S5(SYS_linkat, AT_FDCWD, "ns/f", AT_FDCWD, "ns/g", 0);
	}
}

int fs_search_permission_first(void)
{
	long fd, pid;
	int status, i;

	EXPECT(S3(SYS_mkdirat, AT_FDCWD, "ns", 0755), 0);
	EXPECT(S3(SYS_mkdirat, AT_FDCWD, "ns/sub", 0755), 0);
	OK(fd, S4(SYS_openat, AT_FDCWD, "ns/f", O_CREAT | O_WRONLY, 0666));
	EXPECT(S1(SYS_close, fd), 0);
	/* Readable, so its names can be listed, but not searchable. */
	EXPECT(S4(SYS_fchmodat, AT_FDCWD, "ns", 0666, 0), 0);
	pid = rt_fork();
	if (pid == 0)
	{
		if (S1(SYS_setgid, 1000) != 0 || S1(SYS_setuid, 1000) != 0)
			rt_exit(100);
		for (i = 0; i < NS_CALLS; i++)
		{
			long r = ns_call(i);

			if (r != -EACCES)
			{
				rt_diag("call=", i);
				rt_diag("ret=", r);
				rt_exit(1 + i);
			}
		}
		rt_exit(0);
	}
	CHECK(pid > 0);
	EXPECT(rt_wait(pid, &status, 0), pid);
	(void)S4(SYS_fchmodat, AT_FDCWD, "ns", 0755, 0);
	EXPECT((status >> 8) & 0xff, 0);
	return CONF_PASS;
}
