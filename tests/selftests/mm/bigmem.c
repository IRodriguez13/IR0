/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: bigmem.c
 * Description: Large-RAM selftest, run under QEMU with 4 GiB: MemTotal in
 *              /proc/meminfo reports the machine's RAM, and 1.25 GiB of
 *              anonymous memory can be touched and read back, far above
 *              the old 480 MiB x86-64 / 1 GiB ARM64 frame windows.
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
#include <unistd.h>
#endif

#define BIGMEM_PAGE        4096UL
#define BIGMEM_CHUNK       (64UL << 20)
#define BIGMEM_CHUNKS      20UL /* 1.25 GiB */
/* The runner boots 4 GiB; firmware and the kernel keep less than 1 GiB. */
#define BIGMEM_MIN_TOTAL_KB (3UL << 20)

static char meminfo[2048];

static unsigned long meminfo_total_kb(void)
{
	const char *key = "MemTotal:";
	unsigned long value = 0;
	long n;
	int fd;
	int i;
	int k;

	fd = open("/proc/meminfo", O_RDONLY);
	if (fd < 0)
		return 0;
	n = read(fd, meminfo, sizeof(meminfo) - 1U);
	close(fd);
	if (n <= 0)
		return 0;
	meminfo[n] = '\0';
	for (i = 0; meminfo[i] != '\0'; i++)
	{
		for (k = 0; key[k] != '\0' && meminfo[i + k] == key[k]; k++)
			;
		if (key[k] != '\0')
			continue;
		for (i += k; meminfo[i] == ' '; i++)
			;
		for (; meminfo[i] >= '0' && meminfo[i] <= '9'; i++)
			value = value * 10UL + (unsigned long)(meminfo[i] - '0');
		return value;
	}
	return 0;
}

/* Touch every page of 1.25 GiB, then check one word per page. */
static int touch_big_anon(void)
{
	volatile unsigned long *chunk[BIGMEM_CHUNKS];
	unsigned long c;
	unsigned long off;

	for (c = 0; c < BIGMEM_CHUNKS; c++)
	{
		chunk[c] = mmap(0, BIGMEM_CHUNK, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (chunk[c] == MAP_FAILED)
		{
			ksft_print_num("mmap_chunk", (long)c);
			ksft_print_num("mmap_errno", errno);
			return 0;
		}
		for (off = 0; off < BIGMEM_CHUNK; off += BIGMEM_PAGE)
			chunk[c][off / sizeof(unsigned long)] = (c << 32) | off;
	}
	for (c = 0; c < BIGMEM_CHUNKS; c++)
	{
		for (off = 0; off < BIGMEM_CHUNK; off += BIGMEM_PAGE)
			if (chunk[c][off / sizeof(unsigned long)] != ((c << 32) | off))
			{
				ksft_print_num("readback_chunk", (long)c);
				ksft_print_num("readback_off", (long)off);
				return 0;
			}
		munmap((void *)chunk[c], BIGMEM_CHUNK);
	}
	return 1;
}

int main(void)
{
	unsigned long total_kb;

	ksft_set_plan(2);
	total_kb = meminfo_total_kb();
	ksft_print_num("memtotal_kb", (long)total_kb);
	ksft_test_result(total_kb >= BIGMEM_MIN_TOTAL_KB, "memtotal_reflects_ram");
	ksft_test_result(touch_big_anon(), "touch_1280_mib_anon");
	return ksft_exit();
}
