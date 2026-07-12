/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: syscall_early.c
 * Description: Minimal EL0 SVC — getpid / nanosleep / clock_gettime / write / exit.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "syscall_early.h"
#include "mmu_early.h"
#include "pl011.h"
#include "timer.h"

#include <stdint.h>

#define EBADF  9
#define EFAULT 14
#define EINVAL 22
#define ENOSYS 38

#define WRITE_MAX 256UL
#define NS_PER_SEC 1000000000ULL

struct linux_timespec64
{
	int64_t tv_sec;
	int64_t tv_nsec;
};

static int g_getpid_ok;
static int g_write_ok;
static int g_nanosleep_ok;
static int g_clock_gettime_ok;
static int g_gettimeofday_ok;
static int g_clock_nanosleep_ok;

int arm64_syscall_smoke_ok(void)
{
	return g_getpid_ok && g_write_ok && g_nanosleep_ok && g_clock_gettime_ok &&
	       g_gettimeofday_ok && g_clock_nanosleep_ok;
}

static int64_t sys_getpid(void)
{
	g_getpid_ok = 1;
	return 1;
}

static int64_t sys_write(uint64_t fd, uint64_t buf, uint64_t len)
{
	const char *p;
	uint64_t i;
	uint64_t n;

	if (fd != 1UL)
	{
		return -EBADF;
	}
	if (len == 0)
	{
		return 0;
	}
	n = len > WRITE_MAX ? WRITE_MAX : len;
	if (!arm64_mmu_user_buf_ok(buf, n))
	{
		return -EFAULT;
	}

	p = (const char *)(uintptr_t)buf;
	for (i = 0; i < n; i++)
	{
		char c = p[i];

		if (c == '\n')
		{
			pl011_putc('\r');
		}
		pl011_putc(c);
	}
	g_write_ok = 1;
	return (int64_t)n;
}

/**
 * Busy-wait on CNTPCT for a userspace timespec64 (rem ignored).
 */
static int64_t sleep_timespec_user(uint64_t req, uint64_t rem)
{
	struct linux_timespec64 ts;
	uint64_t frq;
	uint64_t delta;
	uint64_t deadline;
	const volatile uint8_t *src;
	unsigned i;

	(void)rem;

	if (!arm64_mmu_user_buf_ok(req, sizeof(ts)))
	{
		return -EFAULT;
	}

	src = (const volatile uint8_t *)(uintptr_t)req;
	for (i = 0; i < sizeof(ts); i++)
	{
		((uint8_t *)&ts)[i] = src[i];
	}

	if (ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= (int64_t)NS_PER_SEC)
	{
		return -EINVAL;
	}

	frq = arch_timer_get_frequency();
	if (frq == 0)
	{
		return -EINVAL;
	}

	delta = (uint64_t)ts.tv_sec * frq;
	delta += ((uint64_t)ts.tv_nsec * frq) / NS_PER_SEC;
	if (delta == 0)
	{
		delta = 1;
	}

	deadline = arch_timer_read() + delta;
	while (arch_timer_read() < deadline)
	{
		__asm__ volatile("yield" ::: "memory");
	}
	return 0;
}

static int64_t sys_nanosleep(uint64_t req, uint64_t rem)
{
	int64_t ret = sleep_timespec_user(req, rem);

	if (ret == 0)
	{
		g_nanosleep_ok = 1;
		pl011_puts("ARM64_NANOSLEEP_OK\n");
	}
	return ret;
}

static int64_t sys_clock_nanosleep(uint64_t clk_id, uint64_t flags, uint64_t req,
				   uint64_t rem)
{
	int64_t ret;

	(void)flags;
	if (clk_id != ARM64_CLOCK_MONOTONIC)
	{
		return -EINVAL;
	}
	ret = sleep_timespec_user(req, rem);
	if (ret == 0)
	{
		g_clock_nanosleep_ok = 1;
		pl011_puts("ARM64_CLOCK_NANOSLEEP_OK\n");
	}
	return ret;
}

/**
 * Freestanding clock_gettime(CLOCK_MONOTONIC): CNTPCT → timespec64 in userspace.
 */
static int64_t sys_clock_gettime(uint64_t clk_id, uint64_t tp)
{
	struct linux_timespec64 ts;
	uint64_t frq;
	uint64_t pct;
	volatile uint8_t *dst;
	unsigned i;

	if (clk_id != ARM64_CLOCK_MONOTONIC)
	{
		return -EINVAL;
	}
	if (!arm64_mmu_user_buf_ok(tp, sizeof(ts)))
	{
		return -EFAULT;
	}

	frq = arch_timer_get_frequency();
	if (frq == 0)
	{
		return -EINVAL;
	}

	pct = arch_timer_read();
	ts.tv_sec = (int64_t)(pct / frq);
	ts.tv_nsec = (int64_t)(((pct % frq) * NS_PER_SEC) / frq);

	dst = (volatile uint8_t *)(uintptr_t)tp;
	for (i = 0; i < sizeof(ts); i++)
	{
		dst[i] = ((uint8_t *)&ts)[i];
	}

	g_clock_gettime_ok = 1;
	pl011_puts("ARM64_CLOCK_GETTIME_OK\n");
	return 0;
}

struct linux_timeval
{
	int64_t tv_sec;
	int64_t tv_usec;
};

/**
 * Freestanding gettimeofday(tv, tz): tz ignored; CNTPCT → timeval in userspace.
 */
static int64_t sys_gettimeofday(uint64_t tv, uint64_t tz)
{
	struct linux_timeval out;
	uint64_t frq;
	uint64_t pct;
	volatile uint8_t *dst;
	unsigned i;

	(void)tz;

	if (!arm64_mmu_user_buf_ok(tv, sizeof(out)))
	{
		return -EFAULT;
	}

	frq = arch_timer_get_frequency();
	if (frq == 0)
	{
		return -EINVAL;
	}

	pct = arch_timer_read();
	out.tv_sec = (int64_t)(pct / frq);
	out.tv_usec = (int64_t)(((pct % frq) * 1000000ULL) / frq);

	dst = (volatile uint8_t *)(uintptr_t)tv;
	for (i = 0; i < sizeof(out); i++)
	{
		dst[i] = ((uint8_t *)&out)[i];
	}

	g_gettimeofday_ok = 1;
	pl011_puts("ARM64_GETTIMEOFDAY_OK\n");
	return 0;
}

int64_t arm64_syscall_early(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2,
			    uint64_t a3, uint64_t a4, uint64_t a5, int *leave_el0)
{
	(void)a4;
	(void)a5;

	if (leave_el0)
	{
		*leave_el0 = 0;
	}

	switch (nr)
	{
	case ARM64_SYS_GETPID:
		return sys_getpid();
	case ARM64_SYS_NANOSLEEP:
		return sys_nanosleep(a0, a1);
	case ARM64_SYS_CLOCK_GETTIME:
		return sys_clock_gettime(a0, a1);
	case ARM64_SYS_CLOCK_NANOSLEEP:
		return sys_clock_nanosleep(a0, a1, a2, a3);
	case ARM64_SYS_GETTIMEOFDAY:
		return sys_gettimeofday(a0, a1);
	case ARM64_SYS_WRITE:
		return sys_write(a0, a1, a2);
	case ARM64_SYS_EXIT:
		if (leave_el0)
		{
			*leave_el0 = 1;
		}
		if (arm64_syscall_smoke_ok())
		{
			pl011_puts("ARM64_SYSCALL_OK\n");
		}
		else
		{
			pl011_puts("ARM64_SYSCALL_FAIL\n");
		}
		return a0;
	default:
		return -ENOSYS;
	}
}
