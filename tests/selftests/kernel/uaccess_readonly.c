/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: uaccess_readonly.c
 * Description: A syscall never writes its result into a read-only user page
 *              (Linux copy_to_user needs a writable mapping): read(2) into a
 *              PROT_READ mapping or into the program's read-only data fails
 *              with EFAULT and leaves the page intact, also in a fork child
 *              whose read-only pages share frames with its parent.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#endif

#include "ir0/ksft.h"

#ifndef IR0_SELFTEST_NOLIBC
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifndef OPEN_EXISTING_PATH
#define OPEN_EXISTING_PATH "/proc/uptime"
#endif

#define RO_PAGE_SIZE 4096UL
#define PROBE_BYTES 16
#define FILL_BYTE 0x5a

/* errno(3) EFAULT; nolibc has no <errno.h>. */
#define LINUX_EFAULT 14

/* Read-only data of the program image; the two probes use one each. */
static const char ro_self[64] __attribute__((aligned(64))) =
	"IR0 read-only sentinel for the calling process";
static const char ro_shared[64] __attribute__((aligned(64))) =
	"IR0 read-only sentinel shared with a fork child";
static const char ro_self_ref[64] =
	"IR0 read-only sentinel for the calling process";
static const char ro_shared_ref[64] =
	"IR0 read-only sentinel shared with a fork child";

static int bytes_equal(const char *probe, const char *expected,
		       unsigned long n)
{
	const volatile char *p = probe;
	unsigned long i;

	for (i = 0; i < n; i++)
	{
		if (p[i] != expected[i])
		{
			ksft_print_num("changed_at", (long)i);
			return 0;
		}
	}
	return 1;
}

static int read_fails_with_efault(void *dst)
{
	long ret;
	int fd;

	fd = open(OPEN_EXISTING_PATH, O_RDONLY);
	if (fd < 0)
	{
		ksft_print_num("open_errno", errno);
		return 0;
	}
	errno = 0;
	ret = (long)read(fd, dst, PROBE_BYTES);
	close(fd);
	if (ret == -1 && errno == LINUX_EFAULT)
		return 1;
	ksft_print_num("ret", ret);
	ksft_print_num("errno", errno);
	return 0;
}

static int read_into_prot_read_mapping(void)
{
	char expected[PROBE_BYTES];
	volatile char *fill;
	char *p;
	unsigned long i;
	int ok;

	p = mmap(0, RO_PAGE_SIZE, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED)
	{
		ksft_print_num("mmap_errno", errno);
		return 0;
	}
	/* volatile: keep the fills out of a libc memset (nolibc has none). */
	fill = p;
	for (i = 0; i < RO_PAGE_SIZE; i++)
		fill[i] = FILL_BYTE;
	fill = expected;
	for (i = 0; i < PROBE_BYTES; i++)
		fill[i] = FILL_BYTE;
	if (mprotect(p, RO_PAGE_SIZE, PROT_READ) != 0)
	{
		ksft_print_num("mprotect_errno", errno);
		return 0;
	}
	ok = read_fails_with_efault(p);
	return bytes_equal(p, expected, PROBE_BYTES) && ok;
}

static int read_into_rodata(void)
{
	int ok = read_fails_with_efault((void *)ro_self);

	return bytes_equal(ro_self, ro_self_ref, sizeof(ro_self)) && ok;
}

/*
 * fork keeps read-only pages shared without COW: a child that gets the
 * kernel to write into one would change its parent's image too.
 */
static int fork_child_read_into_shared_rodata(void)
{
	int status = 0;
	int intact;
	int pid;

	pid = fork();
	if (pid < 0)
	{
		ksft_print_num("fork_errno", errno);
		return 0;
	}
	if (pid == 0)
		_exit(read_fails_with_efault((void *)ro_shared) ? 0 : 1);
	if (wait4(pid, &status, 0, 0) != pid)
	{
		ksft_print_num("wait4_errno", errno);
		return 0;
	}
	intact = bytes_equal(ro_shared, ro_shared_ref, sizeof(ro_shared));
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
	{
		ksft_print_num("child_status", status);
		return 0;
	}
	return intact;
}

int main(void)
{
	ksft_set_plan(3);
	ksft_test_result(read_into_prot_read_mapping(),
			 "read_into_prot_read_mapping_efault");
	ksft_test_result(read_into_rodata(), "read_into_rodata_efault");
	ksft_test_result(fork_child_read_into_shared_rodata(),
			 "fork_child_read_into_shared_rodata_efault");
	return ksft_exit();
}
