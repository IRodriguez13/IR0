/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: mmap_probe.c
 * Description: Minimal mmap/munmap workload for Linux↔IR0 ABI audit
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#define PAGE_SIZE 4096UL
#define NONE_SIZE 8192UL
#define MAP_OK_MSG "MMAPOK\n"
#define MAP_OK_LEN 7U

#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20
#define MAP_FIXED     0x10

#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_NONE  0x0

#define MMAP_FAILED ((unsigned long long)-1ULL)

static void audit_mmap(unsigned step, const char *op, unsigned long long ret,
		       int err, unsigned len, unsigned long long req,
		       const char *data_hex)
{
	char buf[320];
	int n;

	if (data_hex)
	{
		n = snprintf(buf, sizeof(buf),
			     "[LINUX_ABI_AUDIT][mmap] step=%u op=%s ret=0x%llx errno=%d len=%u req=0x%llx data_hex=%s\n",
			     step, op, ret, err, len, req, data_hex);
	}
	else
	{
		n = snprintf(buf, sizeof(buf),
			     "[LINUX_ABI_AUDIT][mmap] step=%u op=%s ret=0x%llx errno=%d len=%u req=0x%llx\n",
			     step, op, ret, err, len, req);
	}
	if (n > 0)
		(void)write(1, buf, (size_t)n);
}

static int ptr_ok(unsigned long long ret)
{
	return ret != MMAP_FAILED;
}

static unsigned long long do_mmap(void *addr, unsigned long len, int prot,
				  int flags, int fd, unsigned long off)
{
	long r;

	r = syscall(SYS_mmap, addr, len, prot, flags, fd, off);
	return (unsigned long long)r;
}

static unsigned long long pick_fixed_addr(unsigned long long a,
					  unsigned long long b)
{
	unsigned long long lo;

	lo = a < b ? a : b;
	if (lo >= PAGE_SIZE + 0x10000UL)
		return (lo - PAGE_SIZE) & ~(PAGE_SIZE - 1UL);
	return 0x70000000UL;
}

static int hex_encode7(const unsigned char *data, char *hex, size_t hex_sz)
{
	unsigned i;

	if (hex_sz < MAP_OK_LEN * 2U + 1U)
		return -1;
	for (i = 0U; i < MAP_OK_LEN; i++)
		sprintf(hex + (i * 2U), "%02x", data[i]);
	hex[MAP_OK_LEN * 2U] = '\0';
	return 0;
}

int main(void)
{
	unsigned long long map_rw;
	unsigned long long map_none;
	unsigned long long map_fixed;
	unsigned long long fixed_req;
	unsigned long long unret;
	char hex[MAP_OK_LEN * 2U + 1U];
	unsigned char verify[MAP_OK_LEN];

	map_rw = do_mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE,
			 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	audit_mmap(0, "mmap_anon_rw", map_rw, map_rw == MMAP_FAILED ? errno : 0,
		   PAGE_SIZE, 0, NULL);
	if (!ptr_ok(map_rw))
		return 1;

	memcpy((void *)(uintptr_t)map_rw, MAP_OK_MSG, MAP_OK_LEN);
	memcpy(verify, (void *)(uintptr_t)map_rw, MAP_OK_LEN);
	if (memcmp(verify, MAP_OK_MSG, MAP_OK_LEN) != 0)
		return 1;
	if (hex_encode7(verify, hex, sizeof(hex)) < 0)
		return 1;
	audit_mmap(1, "mmap_verify_rw", map_rw, 0, PAGE_SIZE, 0, hex);

	map_none = do_mmap(NULL, NONE_SIZE, PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	audit_mmap(2, "mmap_anon_none", map_none,
		   map_none == MMAP_FAILED ? errno : 0, NONE_SIZE, 0, NULL);
	if (!ptr_ok(map_none))
		return 1;

	fixed_req = pick_fixed_addr(map_rw, map_none);
	map_fixed = do_mmap((void *)(uintptr_t)fixed_req, PAGE_SIZE,
			    PROT_READ | PROT_WRITE,
			    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	audit_mmap(3, "mmap_fixed", map_fixed,
		   map_fixed == MMAP_FAILED ? errno : 0, PAGE_SIZE, fixed_req,
		   NULL);
	if (!ptr_ok(map_fixed) || map_fixed != fixed_req)
		return 1;

	unret = (unsigned long long)syscall(SYS_munmap, (void *)(uintptr_t)map_rw,
					    PAGE_SIZE);
	audit_mmap(4, "munmap_rw", unret, unret == MMAP_FAILED ? errno : 0,
		   PAGE_SIZE, map_rw, NULL);
	if ((long)unret != 0)
		return 1;

	{
		unsigned long long bad =
			do_mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE,
				MAP_PRIVATE, -1, 0);
		audit_mmap(5, "mmap_bad_nanon", bad, errno, PAGE_SIZE, 0, NULL);
		if (ptr_ok(bad) || errno != EBADF)
			return 1;
	}

	{
		unsigned long long bad =
			do_mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE,
				MAP_PRIVATE, 9999, 0);
		audit_mmap(6, "mmap_bad_fd", bad, errno, PAGE_SIZE, 0, NULL);
		if (ptr_ok(bad) || errno != EBADF)
			return 1;
	}

	(void)syscall(SYS_munmap, (void *)(uintptr_t)map_none, NONE_SIZE);
	(void)syscall(SYS_munmap, (void *)(uintptr_t)map_fixed, PAGE_SIZE);

	(void)write(1, "[MMAPOK]\n", 9);
	return 0;
}
