/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: getdents_layout.c
 * Description: getdents(2) records are struct linux_dirent; getdents64(2)
 *              records are struct linux_dirent64.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include "ir0/ksft.h"

#ifdef IR0_SELFTEST_NOLIBC
#include "../../../arch/arm64/include/uapi/asm/unistd.h"
#else
#include "../../../arch/x86-64/include/uapi/asm/unistd.h"
#endif

#ifndef O_CREAT
#define O_CREAT 0100
#endif

/* Linux getdents (NR 78): d_type is the last byte of the record. */
struct linux_dirent {
	unsigned long long d_ino;
	long long d_off;
	unsigned short d_reclen;
	char d_name[];
};

/* Linux getdents64: d_type precedes d_name. */
struct linux_dirent64 {
	unsigned long long d_ino;
	long long d_off;
	unsigned short d_reclen;
	unsigned char d_type;
	char d_name[];
};

static void buf_clear(unsigned char *buf, int n)
{
	volatile unsigned char *z;
	int i;

	z = buf;
	for (i = 0; i < n; i++)
	{
		z[i] = 0;
	}
}

static int name_eq(const char *s, const char *lit)
{
	while (*lit != '\0')
	{
		if (*s != *lit)
			return 0;
		s++;
		lit++;
	}
	return *s == '\0';
}

/* len is strlen(name). Legacy tail is NUL plus d_type; dirent64 tail is NUL. */
static size_t expect_reclen(size_t name_off, size_t len, int legacy)
{
	size_t tail;

	tail = legacy ? 2U : 1U;
	return (name_off + len + tail + 7U) & ~(size_t)7U;
}

static int bounded_len(const char *s, size_t max, size_t *out)
{
	size_t n;

	n = 0;
	while (n < max && s[n] != '\0')
		n++;
	if (n == max || n == 0)
		return 0;
	*out = n;
	return 1;
}

static int records_match(const unsigned char *buf, long n, int legacy,
			 const char *want)
{
	size_t name_off;
	long pos;
	int saw;

	if (legacy)
		name_off = __builtin_offsetof(struct linux_dirent, d_name);
	else
		name_off = __builtin_offsetof(struct linux_dirent64, d_name);
	if (n <= 0)
		return 0;
	pos = 0;
	saw = 0;
	while (pos < n)
	{
		const unsigned char *rec;
		unsigned short reclen;
		size_t room;
		size_t len;
		const char *name;

		rec = buf + pos;
		if (legacy)
			reclen = ((const struct linux_dirent *)rec)->d_reclen;
		else
			reclen = ((const struct linux_dirent64 *)rec)->d_reclen;
		if ((size_t)reclen < name_off + 2U || (reclen & 7U) != 0)
			return 0;
		if (pos > n - (long)reclen)
			return 0;
		name = (const char *)(rec + name_off);
		room = (size_t)reclen - name_off;
		if (legacy)
			room -= 1U;
		if (!bounded_len(name, room, &len))
			return 0;
		if ((size_t)reclen != expect_reclen(name_off, len, legacy))
			return 0;
		if (name_eq(name, want))
			saw = 1;
		pos += (long)reclen;
	}
	return saw && pos == n;
}

static long open_dir(void)
{
	return syscall(__NR_openat, AT_FDCWD, (long)"/gdlayout", O_RDONLY, 0);
}

static int fill_dir(unsigned char *buf, int cap, int legacy)
{
	long fd;
	long n;

	fd = open_dir();
	if (fd < 0)
		return 0;
	buf_clear(buf, cap);
#ifdef __NR_getdents
	if (legacy)
		n = syscall(__NR_getdents, fd, (long)buf, (long)cap);
	else
		n = syscall(__NR_getdents64, fd, (long)buf, (long)cap);
#else
	n = syscall(__NR_getdents64, fd, (long)buf, (long)cap);
#endif
	(void)close((int)fd);
	return records_match(buf, n, legacy, "f");
}

int main(void)
{
	unsigned char buf[512];
	long fd;
	int pass;

	ksft_set_plan(1);
	if (syscall(__NR_mkdirat, AT_FDCWD, (long)"/gdlayout", 0755) != 0)
	{
		ksft_test_result(0, "getdents_layout");
		return ksft_exit();
	}
	fd = syscall(__NR_openat, AT_FDCWD, (long)"/gdlayout/f",
		     O_CREAT | O_WRONLY, 0644);
	if (fd < 0)
	{
		ksft_test_result(0, "getdents_layout");
		return ksft_exit();
	}
	(void)close((int)fd);

	pass = fill_dir(buf, (int)sizeof(buf), 0);
#ifdef __NR_getdents
	/* arch/x86-64/include/uapi/asm/unistd.h defines __NR_getdents. */
	pass = fill_dir(buf, (int)sizeof(buf), 1) && pass;
#endif
	ksft_test_result(pass, "getdents_layout");
	return ksft_exit();
}
