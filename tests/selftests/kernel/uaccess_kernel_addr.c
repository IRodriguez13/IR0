/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: uaccess_kernel_addr.c
 * Description: A syscall handed a pointer to kernel memory fails with EFAULT
 *              in both directions (Linux access_ok + copy_{from,to}_user):
 *              the kernel neither reads it into a user-visible sink nor
 *              writes a result through it.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#endif

#include "ir0/ksft.h"

#ifndef IR0_SELFTEST_NOLIBC
#include <errno.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#define CLOCK_MONOTONIC_ID 1
#define PROBE_BYTES 16

/*
 * Supervisor-only memory that sits inside the low user VA window of every
 * process root: x86_64 maps the keyboard ring at 0x500000 with 4 KiB
 * supervisor pages; arm64 identity-maps the kernel heap at 0x40800000.
 */
#if defined(__x86_64__)
#define KERNEL_ADDR 0x500000UL
#ifndef SYS_clock_gettime
#define SYS_clock_gettime 228
#endif
#elif defined(__aarch64__)
#define KERNEL_ADDR 0x40800000UL
#ifndef SYS_clock_gettime
#define SYS_clock_gettime 113
#endif
#else
#error "uaccess_kernel_addr: unsupported ISA"
#endif

/* errno(3) EFAULT; nolibc has no <errno.h>. */
#define LINUX_EFAULT 14

struct probe_timespec
{
	long tv_sec;
	long tv_nsec;
};

static int failed_with_efault(long ret)
{
	if (ret == -1 && errno == LINUX_EFAULT)
		return 1;
	ksft_print_num("ret", ret);
	ksft_print_num("errno", errno);
	return 0;
}

int main(void)
{
	struct probe_timespec ts = { 0, 0 };
	long ret;

	ksft_set_plan(3);

	ret = (long)syscall(SYS_clock_gettime, CLOCK_MONOTONIC_ID, (long)&ts);
	ksft_test_result(ret == 0, "clock_gettime_user_buffer");

	errno = 0;
	ret = (long)write(1, (const void *)KERNEL_ADDR, PROBE_BYTES);
	ksft_test_result(failed_with_efault(ret), "write_from_kernel_addr_efault");

	errno = 0;
	ret = (long)syscall(SYS_clock_gettime, CLOCK_MONOTONIC_ID,
			    (long)KERNEL_ADDR);
	ksft_test_result(failed_with_efault(ret),
			 "clock_gettime_to_kernel_addr_efault");
	return ksft_exit();
}
