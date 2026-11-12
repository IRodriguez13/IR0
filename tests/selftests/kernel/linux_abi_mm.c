/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: linux_abi_mm.c
 * Description: RED selftest (fails on dev 629036ef, not wired into any gate):
 *              memory-mapping and clock behavior that diverges from Linux,
 *              found by the conformance harness (Documentation/conformance.md).
 *              One case per bug; each passes on Linux. Run by
 *              make ir0-selftest-red-mm-{x86,arm64}.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "conf.h"

#include <sys/mman.h>
#include <time.h>

#define PG RT_PAGE

/* The kernel still runs processes: a forked child exits with 42. */
static int kernel_alive(void)
{
	int status = 0;
	long pid = rt_fork();

	if (pid == 0)
		rt_exit(42);
	if (pid < 0)
		return 0;
	return conf_child_status(pid, &status, 5000) == pid && RT_EXITED(status) &&
	       RT_EXITCODE(status) == 42;
}

/* Run op() in a child; it may die (it can unmap its own pages). */
static int in_child(void (*op)(void))
{
	int status = 0;
	long pid = rt_fork();

	if (pid == 0)
	{
		op();
		rt_exit(0);
	}
	if (pid < 0)
		return 0;
	return conf_child_status(pid, &status, 5000) == pid;
}

static long map_anon(int prot, int flags)
{
	return S6(SYS_mmap, 0, PG, prot, flags | MAP_ANONYMOUS, -1, 0);
}

/* mm/mmap.c do_mmap(): neither MAP_PRIVATE nor MAP_SHARED is EINVAL. */
int mm_mmap_requires_private_or_shared(void)
{
	EXPECT_ERR(map_anon(PROT_READ, 0), EINVAL);
	return CONF_PASS;
}

/* mmap(2): a request beyond the commit limit is ENOMEM, not EINVAL. */
int mm_mmap_huge_enomem(void)
{
	EXPECT_ERR(S6(SYS_mmap, 0, 1UL << 40, PROT_READ | PROT_WRITE,
		      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0), ENOMEM);
	return CONF_PASS;
}

/* munmap(2): unmapping a range with no mapping succeeds. */
int mm_munmap_hole_succeeds(void)
{
	long a = map_anon(PROT_READ, MAP_PRIVATE);

	CHECK(a > 0);
	EXPECT(S2(SYS_munmap, a, PG), 0);
	EXPECT(S2(SYS_munmap, a, PG), 0);
	return CONF_PASS;
}

/* munmap(2): an address that is not page-aligned is EINVAL. */
int mm_munmap_unaligned_einval(void)
{
	long a = map_anon(PROT_READ | PROT_WRITE, MAP_PRIVATE);

	CHECK(a > 0);
	EXPECT_ERR(S2(SYS_munmap, a + 1, PG), EINVAL);
	*(volatile char *)a = 1; /* the page must still be mapped */
	EXPECT(S2(SYS_munmap, a, PG), 0);
	return CONF_PASS;
}

/* mprotect(2) PROT_NONE: any access faults with SIGSEGV. */
int mm_prot_none_faults(void)
{
	int status = 0;
	long a = map_anon(PROT_READ | PROT_WRITE, MAP_PRIVATE);
	long pid;

	CHECK(a > 0);
	*(volatile char *)a = 1;
	EXPECT(S3(SYS_mprotect, a, PG, PROT_NONE), 0);
	pid = rt_fork();
	CHECK(pid >= 0);
	if (pid == 0)
	{
		(void)*(volatile char *)a;
		rt_exit(0);
	}
	EXPECT(conf_child_status(pid, &status, 5000), pid);
	CHECK(RT_SIGNALED(status) && RT_TERMSIG(status) == SIGSEGV);
	return CONF_PASS;
}

/* MAP_SHARED | MAP_ANONYMOUS memory is shared with a forked child. */
int mm_shared_anon_survives_fork(void)
{
	int status = 0;
	long a = map_anon(PROT_READ | PROT_WRITE, MAP_SHARED);
	long pid;

	CHECK(a > 0);
	*(volatile char *)a = 'P';
	pid = rt_fork();
	CHECK(pid >= 0);
	if (pid == 0)
	{
		*(volatile char *)a = 'C';
		rt_exit(0);
	}
	EXPECT(conf_child_status(pid, &status, 5000), pid);
	EXPECT(*(volatile char *)a, 'C');
	return CONF_PASS;
}

/* clock_gettime(2) supports CLOCK_PROCESS_CPUTIME_ID. */
int time_clock_process_cputime(void)
{
	struct timespec ts;

	EXPECT(S2(SYS_clock_gettime, CLOCK_PROCESS_CPUTIME_ID, &ts), 0);
	return CONF_PASS;
}

/* CLOCK_REALTIME follows the RTC (QEMU starts it at host time). */
int time_realtime_from_rtc(void)
{
	struct timespec ts;

	EXPECT(S2(SYS_clock_gettime, CLOCK_REALTIME, &ts), 0);
	CHECK(ts.tv_sec > 1600000000L);
	return CONF_PASS;
}

static void on_segv(int sig)
{
	(void)sig;
	rt_exit(77);
}

/* Unmap the text page that holds the SIGSEGV handler, then fault. */
static void unmap_own_handler(void)
{
	unsigned long page = (unsigned long)on_segv & ~(PG - 1);

	(void)rt_sigaction(SIGSEGV, on_segv, 0, 0);
	if (S2(SYS_munmap, page, PG) != 0)
		rt_exit(60); /* munmap refused the text page */
	*(volatile char *)0 = 1;
}

/* A read-only page of the image (.rodata), shared by fork. */
static const char ro_page[RT_PAGE] __attribute__((aligned(RT_PAGE))) = { 'P' };

/*
 * mm/mprotect.c + do_wp_page(): a page fork() shares read-only stays private
 * to each process. Making it writable in the child must not let the child
 * write the parent's frame.
 */
int mm_fork_ro_page_stays_private(void)
{
	int status = 0;
	unsigned long page = (unsigned long)ro_page;
	long pid = rt_fork();

	CHECK(pid >= 0);
	if (pid == 0)
	{
		if (S3(SYS_mprotect, page, PG, PROT_READ | PROT_WRITE) != 0)
			rt_exit(60);
		*(volatile char *)page = 'C';
		rt_exit(0);
	}
	EXPECT(conf_child_status(pid, &status, 5000), pid);
	EXPECT(RT_EXITED(status) ? RT_EXITCODE(status) : -1, 0);
	EXPECT(*(volatile const char *)page, 'P');
	return CONF_PASS;
}

static void map_over_supervisor_page(void)
{
	long a = S6(SYS_mmap, RT_SUPERVISOR_ADDR, PG, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);

	if (a == (long)RT_SUPERVISOR_ADDR)
		*(volatile char *)a = 1;
}

/*
 * kernel/signal.c force_sigsegv(): when the SIGSEGV handler itself cannot
 * run (its page is unmapped), Linux kills the task with SIGSEGV. The fault
 * must not loop and must leave the kernel running.
 */
int mm_unmapped_segv_handler_kills_task(void)
{
	int status = 0;
	long pid = rt_fork();

	CHECK(pid >= 0);
	if (pid == 0)
	{
		unmap_own_handler();
		rt_exit(0);
	}
	EXPECT(conf_child_status(pid, &status, 5000), pid);
	/* 60: munmap failed; 77: the "unmapped" handler still ran. */
	EXPECT(RT_EXITED(status) ? RT_EXITCODE(status) : 0, 0);
	EXPECT(RT_TERMSIG(status), SIGSEGV);
	CHECK(kernel_alive());
	return CONF_PASS;
}

/*
 * A mapping of the process inside the range keeps munmap from rejecting it
 * as empty: mmap(NULL) lands below the stack, within 1 GiB of the address.
 */
static void unmap_supervisor_range(void)
{
	(void)map_anon(PROT_READ | PROT_WRITE, MAP_PRIVATE);
	(void)S2(SYS_munmap, RT_SUPERVISOR_ADDR, 1UL << 30);
}

/*
 * The kernel pages IR0 maps into every process root (arm64: the
 * identity-mapped heap at 0x40800000; x86_64: 0x500000) are not user VMAs:
 * mmap(2) MAP_FIXED over them must leave the kernel running. On Linux the
 * range is plain user address space. On IR0 arm64 the guest hangs.
 */
int mm_mmap_fixed_over_kernel_pages_is_harmless(void)
{
	CHECK(in_child(map_over_supervisor_page));
	CHECK(kernel_alive());
	return CONF_PASS;
}

/*
 * Same contract for munmap(2): unmapping a range that covers those kernel
 * pages (the fuzzer's munmap(0x40800000, 1 GiB) on arm64) must leave the
 * kernel running. On IR0 arm64 the guest stops.
 */
int mm_munmap_over_kernel_pages_is_harmless(void)
{
	CHECK(in_child(unmap_supervisor_range));
	CHECK(kernel_alive());
	return CONF_PASS;
}
