/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: cases_mm.c
 * Description: Conformance cases for mmap/munmap/mprotect/brk/madvise and
 *              the time syscalls. Expected results follow Linux (mm/mmap.c,
 *              mm/mprotect.c, mm/madvise.c, kernel/time/) and the
 *              man-pages ERRORS sections.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "conf.h"

#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>

#define WAIT_MS 5000L
#define PG RT_PAGE

static long map_anon(long pages, int prot)
{
	return S6(SYS_mmap, 0, pages * PG, prot, MAP_PRIVATE | MAP_ANONYMOUS, -1,
		  0);
}

/* Run touch(addr, write) in a child; return its termination signal. */
static long child_touch(volatile char *addr, int write)
{
	int status = 0;
	long pid = rt_fork();
	long ret;

	if (pid < 0)
		return pid;
	if (pid == 0)
	{
		if (write)
			*addr = 1;
		else
			(void)*addr;
		rt_exit(0);
	}
	ret = conf_child_status(pid, &status, WAIT_MS);
	if (ret != pid)
		return ret;
	return RT_SIGNALED(status) ? RT_TERMSIG(status) : 0;
}

int mm_mmap_anon_zeroed(void)
{
	volatile unsigned char *p;
	long a;
	unsigned long i;

	OK(a, map_anon(4, PROT_READ | PROT_WRITE));
	CHECK((a & (PG - 1)) == 0);
	p = (volatile unsigned char *)a;
	for (i = 0; i < 4 * PG; i += 512)
		CHECK(p[i] == 0);
	p[0] = 0xaa;
	p[4 * PG - 1] = 0x55;
	CHECK(p[0] == 0xaa && p[4 * PG - 1] == 0x55);
	EXPECT(S2(SYS_munmap, a, 4 * PG), 0);
	return CONF_PASS;
}

int mm_mmap_einval(void)
{
	long a;

	EXPECT_ERR(S6(SYS_mmap, 0, 0, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1,
		      0), EINVAL);
	EXPECT_ERR(S6(SYS_mmap, 0, PG, PROT_READ, MAP_ANONYMOUS, -1, 0), EINVAL);
	OK(a, map_anon(1, PROT_READ));
	EXPECT_ERR(S6(SYS_mmap, a + 1, PG, PROT_READ,
		      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0), EINVAL);
	EXPECT_ERR(S6(SYS_mmap, 0, PG, PROT_READ, MAP_PRIVATE, -1, 0), EBADF);
	EXPECT_ERR(S6(SYS_mmap, 0, PG, PROT_READ, MAP_PRIVATE, 4000, 0), EBADF);
	EXPECT_ERR(S2(SYS_munmap, a + 1, PG), EINVAL);
	EXPECT_ERR(S2(SYS_munmap, a, 0), EINVAL);
	return CONF_PASS;
}

int mm_mmap_huge_enomem(void)
{
	/* 1 TiB of private anonymous memory exceeds any guest commit limit. */
	EXPECT_ERR(S6(SYS_mmap, 0, 1UL << 40, PROT_READ | PROT_WRITE,
		      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0), ENOMEM);
	return CONF_PASS;
}

int mm_munmap_then_segv(void)
{
	long a;

	OK(a, map_anon(1, PROT_READ | PROT_WRITE));
	*(volatile char *)a = 1;
	EXPECT(S2(SYS_munmap, a, PG), 0);
	EXPECT(child_touch((volatile char *)a, 0), SIGSEGV);
	/* Unmapping a hole again is not an error. */
	EXPECT(S2(SYS_munmap, a, PG), 0);
	return CONF_PASS;
}

int mm_munmap_middle(void)
{
	volatile char *p;
	long a;

	OK(a, map_anon(3, PROT_READ | PROT_WRITE));
	p = (volatile char *)a;
	p[0] = 1;
	p[PG] = 2;
	p[2 * PG] = 3;
	EXPECT(S2(SYS_munmap, a + PG, PG), 0);
	CHECK(p[0] == 1 && p[2 * PG] == 3);
	EXPECT(child_touch(p + PG, 0), SIGSEGV);
	return CONF_PASS;
}

int mm_mprotect_readonly(void)
{
	volatile char *p;
	long a;

	OK(a, map_anon(1, PROT_READ | PROT_WRITE));
	p = (volatile char *)a;
	p[0] = 7;
	EXPECT(S3(SYS_mprotect, a, PG, PROT_READ), 0);
	CHECK(p[0] == 7);
	EXPECT(child_touch(p, 1), SIGSEGV);
	EXPECT(S3(SYS_mprotect, a, PG, PROT_NONE), 0);
	EXPECT(child_touch(p, 0), SIGSEGV);
	EXPECT(S3(SYS_mprotect, a, PG, PROT_READ | PROT_WRITE), 0);
	p[0] = 8;
	CHECK(p[0] == 8);
	return CONF_PASS;
}

int mm_mprotect_errors(void)
{
	long a;

	OK(a, map_anon(1, PROT_READ | PROT_WRITE));
	EXPECT_ERR(S3(SYS_mprotect, a + 1, PG, PROT_READ), EINVAL);
	EXPECT_ERR(S3(SYS_mprotect, a, PG, 0x7f000000), EINVAL);
	EXPECT(S2(SYS_munmap, a, PG), 0);
	EXPECT_ERR(S3(SYS_mprotect, a, PG, PROT_READ), ENOMEM);
	return CONF_PASS;
}

int mm_mmap_fixed_replaces(void)
{
	volatile char *p;
	long a;
	long b;

	OK(a, map_anon(2, PROT_READ | PROT_WRITE));
	p = (volatile char *)a;
	p[0] = 9;
	p[PG] = 9;
	OK(b, S6(SYS_mmap, a, PG, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0));
	EXPECT(b, a);
	CHECK(p[0] == 0);
	CHECK(p[PG] == 9);
	return CONF_PASS;
}

int mm_mmap_file_private(void)
{
	char buf[8];
	volatile char *p;
	long fd;
	long a;

	OK(fd, conf_creat("f", "filedata"));
	OK(a, S6(SYS_mmap, 0, PG, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0));
	p = (volatile char *)a;
	CHECK(rt_memeq((const void *)a, "filedata", 8));
	CHECK(p[8] == 0 && p[PG - 1] == 0);
	p[0] = 'X';
	EXPECT(S4(SYS_pread64, fd, buf, 8, 0), 8);
	CHECK(buf[0] == 'f');
	EXPECT_ERR(S6(SYS_mmap, 0, PG, PROT_READ, MAP_PRIVATE, fd, 1), EINVAL);
	return CONF_PASS;
}

int mm_mmap_file_shared(void)
{
	char buf[8];
	volatile char *p;
	long fd;
	long a;

	OK(fd, conf_creat("f", "filedata"));
	OK(a, S6(SYS_mmap, 0, PG, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
	p = (volatile char *)a;
	p[0] = 'S';
	EXPECT(S3(SYS_msync, a, PG, MS_SYNC), 0);
	EXPECT(S4(SYS_pread64, fd, buf, 8, 0), 8);
	CHECK(buf[0] == 'S');
	return CONF_PASS;
}

int mm_mmap_access_mode(void)
{
	long fd;

	OK(fd, conf_creat("f", "x"));
	EXPECT(S1(SYS_close, fd), 0);
	OK(fd, S4(SYS_openat, AT_FDCWD, "f", O_RDONLY, 0));
	EXPECT_ERR(S6(SYS_mmap, 0, PG, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0),
		   EACCES);
	return CONF_PASS;
}

int mm_brk(void)
{
	volatile char *p;
	long cur;
	long next;

	OK(cur, S1(SYS_brk, 0));
	CHECK(cur > 0);
	OK(next, S1(SYS_brk, cur + 4 * PG));
	EXPECT(next, cur + 4 * PG);
	p = (volatile char *)cur;
	p[0] = 1;
	p[4 * PG - 1] = 2;
	CHECK(p[4 * PG - 1] == 2);
	EXPECT(S1(SYS_brk, cur), cur);
	/* An impossible request returns the unchanged break. */
	EXPECT(S1(SYS_brk, RT_KERNEL_ADDR_A), cur);
	return CONF_PASS;
}

int mm_madvise_dontneed(void)
{
	volatile char *p;
	long a;

	OK(a, map_anon(2, PROT_READ | PROT_WRITE));
	p = (volatile char *)a;
	p[0] = 5;
	EXPECT(S3(SYS_madvise, a, 2 * PG, MADV_DONTNEED), 0);
	CHECK(p[0] == 0);
	EXPECT_ERR(S3(SYS_madvise, a + 1, PG, MADV_DONTNEED), EINVAL);
	EXPECT_ERR(S3(SYS_madvise, a, PG, 12345), EINVAL);
	return CONF_PASS;
}

int mm_fork_cow_isolation(void)
{
	volatile char *p;
	int status = 0;
	long a;
	long pid;

	OK(a, map_anon(1, PROT_READ | PROT_WRITE));
	p = (volatile char *)a;
	p[0] = 'P';
	OK(pid, rt_fork());
	if (pid == 0)
	{
		rt_sleep_ms(50);
		if (p[0] != 'P')
			rt_exit(1);
		p[0] = 'C';
		rt_exit(0);
	}
	p[0] = 'Q';
	EXPECT(conf_child_status(pid, &status, WAIT_MS), pid);
	EXPECT(status, 0);
	CHECK(p[0] == 'Q');
	return CONF_PASS;
}

int mm_shared_anon_fork(void)
{
	volatile char *p;
	int status = 0;
	long a;
	long pid;

	OK(a, S6(SYS_mmap, 0, PG, PROT_READ | PROT_WRITE,
		 MAP_SHARED | MAP_ANONYMOUS, -1, 0));
	p = (volatile char *)a;
	p[0] = 'P';
	OK(pid, rt_fork());
	if (pid == 0)
	{
		p[0] = 'C';
		rt_exit(0);
	}
	EXPECT(conf_child_status(pid, &status, WAIT_MS), pid);
	CHECK(p[0] == 'C');
	return CONF_PASS;
}

int time_clock_gettime(void)
{
	struct timespec a;
	struct timespec b;

	EXPECT(S2(SYS_clock_gettime, CLOCK_MONOTONIC, &a), 0);
	EXPECT(S2(SYS_clock_gettime, CLOCK_MONOTONIC, &b), 0);
	CHECK(a.tv_nsec >= 0 && a.tv_nsec < 1000000000L);
	CHECK(b.tv_sec > a.tv_sec ||
	      (b.tv_sec == a.tv_sec && b.tv_nsec >= a.tv_nsec));
	EXPECT(S2(SYS_clock_gettime, CLOCK_REALTIME, &a), 0);
	/* QEMU's RTC follows the host clock: after 2020-09-13. */
	CHECK(a.tv_sec > 1600000000L);
	EXPECT(S2(SYS_clock_gettime, CLOCK_BOOTTIME, &a), 0);
	EXPECT(S2(SYS_clock_gettime, CLOCK_PROCESS_CPUTIME_ID, &a), 0);
	return CONF_PASS;
}

int time_clock_errors(void)
{
	struct timespec ts;

	EXPECT_ERR(S2(SYS_clock_gettime, 1000, &ts), EINVAL);
	EXPECT_ERR(S2(SYS_clock_gettime, CLOCK_MONOTONIC, 0), EFAULT);
	EXPECT_ERR(S2(SYS_clock_gettime, CLOCK_MONOTONIC, RT_KERNEL_ADDR_A),
		   EFAULT);
	return CONF_PASS;
}

int time_clock_getres(void)
{
	struct timespec ts = { 9, 9 };

	EXPECT(S2(SYS_clock_getres, CLOCK_MONOTONIC, &ts), 0);
	CHECK(ts.tv_sec == 0 && ts.tv_nsec > 0 && ts.tv_nsec <= 10000000L);
	EXPECT(S2(SYS_clock_getres, CLOCK_REALTIME, 0), 0);
	EXPECT_ERR(S2(SYS_clock_getres, 1000, &ts), EINVAL);
	return CONF_PASS;
}

int time_nanosleep(void)
{
	struct timespec req = { 0, 30000000L };
	long t0;
	long t1;

	OK(t0, rt_now_ms());
	EXPECT(S2(SYS_nanosleep, &req, 0), 0);
	OK(t1, rt_now_ms());
	CHECK(t1 - t0 >= 29);
	rt_diag("slept_ms", t1 - t0);
	req.tv_nsec = 1000000000L;
	EXPECT_ERR(S2(SYS_nanosleep, &req, 0), EINVAL);
	req.tv_sec = -1;
	req.tv_nsec = 0;
	EXPECT_ERR(S2(SYS_nanosleep, &req, 0), EINVAL);
	EXPECT_ERR(S2(SYS_nanosleep, 0, 0), EFAULT);
	return CONF_PASS;
}

int time_gettimeofday(void)
{
	struct timeval tv;
	struct timespec ts;

	EXPECT(S2(SYS_gettimeofday, &tv, 0), 0);
	EXPECT(S2(SYS_clock_gettime, CLOCK_REALTIME, &ts), 0);
	CHECK(tv.tv_usec >= 0 && tv.tv_usec < 1000000L);
	CHECK(ts.tv_sec - tv.tv_sec >= 0 && ts.tv_sec - tv.tv_sec <= 1);
	return CONF_PASS;
}
