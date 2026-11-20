/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: vm_commit.c
 * Description: MAP_NORESERVE skips commit; PROT_NONE to RW does not populate.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#endif

#include "ir0/ksft.h"

#ifndef IR0_SELFTEST_NOLIBC
#include <errno.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

/* Linux asm-generic/mman-common.h and errno.h. */
#define LINUX_MAP_NORESERVE 0x4000
#define LINUX_ENOMEM 12
#define LINUX_PROT_NONE 0

#ifndef SYS_sysinfo
#ifdef __aarch64__
#define SYS_sysinfo 179
#else
#define SYS_sysinfo 99
#endif
#endif

#define SYSINFO_BYTES 112
#define SYSINFO_FREERAM 40
#define SYSINFO_MEM_UNIT 104
#define PAGE 4096UL

static long ir0_sysinfo(void *info)
{
	return (long)syscall(SYS_sysinfo, info);
}

static void load_mem(void *dst, const unsigned char *src, unsigned n)
{
	unsigned char *d = dst;
	unsigned i;

	for (i = 0; i < n; i++)
		d[i] = src[i];
}

static int free_bytes(unsigned long long *out)
{
	unsigned char raw[SYSINFO_BYTES];
	unsigned long long pages;
	unsigned int unit;

	if (ir0_sysinfo(raw) != 0)
		return -1;
	load_mem(&pages, raw + SYSINFO_FREERAM, sizeof(pages));
	load_mem(&unit, raw + SYSINFO_MEM_UNIT, sizeof(unit));
	if (unit == 0)
		unit = 1;
	*out = pages * (unsigned long long)unit;
	return 0;
}

int main(void)
{
	unsigned long long free0;
	unsigned long long before;
	unsigned long long after;
	unsigned long long drop;
	unsigned long len;
	unsigned long chunk;
	void *p;
	int pass;
	int mp;

	ksft_set_plan(3);

	pass = free_bytes(&free0) == 0 && free0 > (4UL * PAGE);
	if (pass)
	{
		len = (unsigned long)free0 + (16UL * PAGE);
		len = (len + PAGE - 1UL) & ~(PAGE - 1UL);
		/* The kernel still rejects a single mmap above 256 MiB. */
		pass = len < (1UL << 28);
	}
	if (pass)
	{
		p = mmap(0, len, PROT_READ | PROT_WRITE,
			 MAP_PRIVATE | MAP_ANONYMOUS | LINUX_MAP_NORESERVE,
			 -1, 0);
		pass = p != MAP_FAILED;
		if (pass)
		{
			*(volatile unsigned char *)p = 0x11;
			pass = *(volatile unsigned char *)p == 0x11;
			munmap(p, len);
		}
	}
	ksft_test_result(pass, "noreserve_over_free");
	if (!pass)
		return ksft_exit();

	p = mmap(0, len, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	pass = p == MAP_FAILED && errno == LINUX_ENOMEM;
	if (p != MAP_FAILED)
		munmap(p, len);
	ksft_test_result(pass, "reserve_over_free_enomem");
	if (!pass)
		return ksft_exit();

	chunk = 8UL * 1024UL * 1024UL;
	pass = free_bytes(&before) == 0;
	p = mmap(0, chunk, LINUX_PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	mp = -1;
	if (p != MAP_FAILED)
		mp = mprotect(p, chunk, PROT_READ | PROT_WRITE);
	pass = pass && p != MAP_FAILED && mp == 0 && free_bytes(&after) == 0;
	if (pass)
	{
		drop = (before > after) ? (before - after) : 0;
		pass = drop < (chunk / 4UL);
		*(volatile unsigned char *)p = 0x5a;
		pass = pass && *(volatile unsigned char *)p == 0x5a;
	}
	if (p != MAP_FAILED)
		munmap(p, chunk);
	ksft_test_result(pass, "mprotect_lazy");
	return ksft_exit();
}
