/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: init_fork_mem_smoke.c
 * Description: IR0 kernel source — init fork mem smoke
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string.h>

#define FASE40_HEAP_HINT ((void *)0x09000000UL)
#define FASE40_MMAP_HINT ((void *)0x09010000UL)
#define FASE40_STORM_CHILDREN 8
#define FASE40_CHILD_ALLOC (64 * 1024)

static void write_str(const char *s)
{
	const char *p = s;

	while (*p)
		p++;
	(void)write(1, s, (size_t)(p - s));
}

static void write_hex_u64(uint64_t v)
{
	static const char hex[] = "0123456789ABCDEF";
	char out[18];

	out[0] = '0';
	out[1] = 'x';
	for (int i = 0; i < 16; i++)
	{
		unsigned shift = (unsigned)(60 - (i * 4));

		out[2 + i] = hex[(v >> shift) & 0xFU];
	}
	(void)write(1, out, sizeof(out));
}

static void write_dec_u64(uint64_t v)
{
	char buf[32];
	int n = 0;

	if (v == 0)
	{
		(void)write(1, "0", 1);
		return;
	}

	while (v > 0 && n < (int)sizeof(buf))
	{
		buf[n++] = (char)('0' + (v % 10U));
		v /= 10U;
	}
	while (n-- > 0)
		(void)write(1, &buf[n], 1);
}

static long read_used_kb(void)
{
	char buf[128];
	int fd;
	ssize_t n;
	long vals[3] = {0, 0, 0};
	int vi = 0;
	long cur = 0;
	int in_num = 0;

	fd = open("/proc/meminfo", O_RDONLY);
	if (fd < 0)
		return -1;
	n = read(fd, buf, sizeof(buf) - 1);
	(void)close(fd);
	if (n <= 0)
		return -1;
	buf[n] = '\0';

	for (ssize_t i = 0; i < n && vi < 3; i++)
	{
		char c = buf[i];
		if (c >= '0' && c <= '9')
		{
			in_num = 1;
			cur = cur * 10 + (long)(c - '0');
		}
		else if (in_num)
		{
			vals[vi++] = cur;
			cur = 0;
			in_num = 0;
		}
	}
	if (in_num && vi < 3)
		vals[vi++] = cur;

	if (vi < 3)
		return -1;
	return vals[2];
}

static int test_heap_isolation(void)
{
	char *p = (char *)mmap(FASE40_HEAP_HINT, 4096, PROT_READ | PROT_WRITE,
			       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	pid_t pid;
	int status = 0;
	pid_t wr;
	int child_exit;
	int parent_ok;

	if (p == MAP_FAILED)
	{
		write_str("FASE40_A FAIL mmap\n");
		return 2;
	}

	strcpy(p, "parent");
	pid = fork();
	if (pid == 0)
	{
		strcpy(p, "child");
		write_str("FASE40_A child_va=");
		write_hex_u64((uint64_t)(uintptr_t)p);
		write_str("\n");
		_exit(memcmp(p, "child", 5) == 0 ? 0 : 1);
	}
	if (pid < 0)
	{
		write_str("FASE40_A FAIL fork\n");
		(void)munmap(p, 4096);
		return 3;
	}

	wr = wait4(pid, &status, 0, 0);
	child_exit = (wr < 0) ? 255 : ((status >> 8) & 0xFF);
	parent_ok = (memcmp(p, "parent", 6) == 0);

	write_str("FASE40_A parent_va=");
	write_hex_u64((uint64_t)(uintptr_t)p);
	write_str(" child_status=");
	write_dec_u64((uint64_t)child_exit);
	write_str(" wait_ret=");
	write_dec_u64((uint64_t)wr);
	write_str(" parent_string=");
	write_str(p);
	write_str("\n");

	(void)munmap(p, 4096);
	return parent_ok ? 0 : 4;
}

static int test_stack_isolation(void)
{
	volatile int x = 10;
	pid_t pid;
	int status = 0;
	pid_t wr;
	int parent_ok = 0;

	pid = fork();
	if (pid == 0)
	{
		x = 20;
		write_str("FASE40_B child_stack_va=");
		write_hex_u64((uint64_t)(uintptr_t)&x);
		write_str(" child_x=");
		write_dec_u64((uint64_t)x);
		write_str("\n");
		_exit(0);
	}
	if (pid < 0)
	{
		write_str("FASE40_B FAIL fork\n");
		return 2;
	}

	wr = wait4(pid, &status, 0, 0);
	parent_ok = (x == 10);

	write_str("FASE40_B parent_stack_va=");
	write_hex_u64((uint64_t)(uintptr_t)&x);
	write_str(" parent_x=");
	write_dec_u64((uint64_t)x);
	write_str(" same_content=");
	write_str(parent_ok ? "1" : "0");
	write_str(" wait_ret=");
	write_dec_u64((uint64_t)wr);
	write_str("\n");

	return parent_ok ? 0 : 3;
}

static int test_mmap_fork_semantics(void)
{
	char *p = (char *)mmap(FASE40_MMAP_HINT, 4096, PROT_READ | PROT_WRITE,
			       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	pid_t pid;
	int status = 0;
	pid_t wr;
	int child_exit;
	const char *klass = "BROKEN";

	if (p == MAP_FAILED)
	{
		write_str("FASE40_C FAIL mmap\n");
		return 2;
	}

	strcpy(p, "parent-c");
	pid = fork();
	if (pid == 0)
	{
		strcpy(p, "child-c");
		_exit(memcmp(p, "child-c", 7) == 0 ? 0 : 1);
	}
	if (pid < 0)
	{
		write_str("FASE40_C FAIL fork\n");
		(void)munmap(p, 4096);
		return 3;
	}

	wr = wait4(pid, &status, 0, 0);
	child_exit = (wr < 0) ? 255 : ((status >> 8) & 0xFF);
	if (child_exit == 0 && memcmp(p, "parent-c", 8) == 0)
		klass = "PRIVATE";
	else if (memcmp(p, "child-c", 7) == 0)
		klass = "SHARED";

	write_str("FASE40_C va=");
	write_hex_u64((uint64_t)(uintptr_t)p);
	write_str(" class=");
	write_str(klass);
	write_str(" parent_view=");
	write_str(p);
	write_str(" child_status=");
	write_dec_u64((uint64_t)child_exit);
	write_str(" wait_ret=");
	write_dec_u64((uint64_t)wr);
	write_str("\n");

	(void)munmap(p, 4096);
	return (klass[0] == 'P') ? 0 : 4;
}

static int test_memory_growth(void)
{
	long used_before_kb = read_used_kb();
	long used_peak_kb;
	long used_after_kb;
	pid_t kids[FASE40_STORM_CHILDREN];
	int started = 0;

	for (int i = 0; i < FASE40_STORM_CHILDREN; i++)
	{
		pid_t pid = fork();
		if (pid == 0)
		{
			volatile unsigned char *m;

			m = (volatile unsigned char *)mmap(NULL, FASE40_CHILD_ALLOC,
						      PROT_READ | PROT_WRITE,
						      MAP_PRIVATE | MAP_ANONYMOUS,
						      -1, 0);
			if (m != MAP_FAILED)
			{
				for (size_t o = 0; o < FASE40_CHILD_ALLOC; o += 4096)
					m[o] = (unsigned char)(o >> 12);
			}
			_exit(0);
		}
		if (pid < 0)
			break;
		kids[started++] = pid;
	}

	used_peak_kb = read_used_kb();

	for (int i = 0; i < started; i++)
		(void)wait4(kids[i], 0, 0, 0);

	used_after_kb = read_used_kb();

	{
		uint64_t pages_before = (used_before_kb > 0) ? (uint64_t)(used_before_kb / 4) : 0;
		uint64_t pages_peak = (used_peak_kb > 0) ? (uint64_t)(used_peak_kb / 4) : 0;
		uint64_t pages_after = (used_after_kb > 0) ? (uint64_t)(used_after_kb / 4) : 0;
		uint64_t pages_freed = (pages_peak > pages_after) ? (pages_peak - pages_after) : 0;
		uint64_t leaks = (pages_after > pages_before) ? (pages_after - pages_before) : 0;

		write_str("FASE40_D pages_before=");
		write_dec_u64(pages_before);
		write_str(" pages_after=");
		write_dec_u64(pages_after);
		write_str(" pages_freed=");
		write_dec_u64(pages_freed);
		write_str(" leaks=");
		write_dec_u64(leaks);
		write_str(" children=");
		write_dec_u64((uint64_t)started);
		write_str("\n");

		return (leaks == 0) ? 0 : 5;
	}
}

int main(void)
{
	int ra;
	int rb;
	int rc;
	int rd;

	ra = test_heap_isolation();
	rb = test_stack_isolation();
	rc = test_mmap_fork_semantics();
	rd = test_memory_growth();

	write_str("FASE40_SUMMARY A=");
	write_dec_u64((uint64_t)ra);
	write_str(" B=");
	write_dec_u64((uint64_t)rb);
	write_str(" C=");
	write_dec_u64((uint64_t)rc);
	write_str(" D=");
	write_dec_u64((uint64_t)rd);
	write_str("\n");

	if (ra || rb || rc || rd)
		return 1;
	return 0;
}
