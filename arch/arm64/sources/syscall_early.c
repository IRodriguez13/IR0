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
#include "elf_load_early.h"
#include "rootfs_early.h"
#include "mmu_early.h"
#include "pl011.h"
#include "timer.h"

#include <stdint.h>

#define EBADF  9
#define EFAULT 14
#define EINVAL 22
#define ENOSYS 38
#define ENOMEM 12
#define ENOENT 2

#define WRITE_MAX 256UL
#define NS_PER_SEC 1000000000ULL

/* Extra Linux aarch64 numbers used by musl / BusyBox static CRT. */
#define ARM64_SYS_GETTID           178UL
#define ARM64_SYS_SET_TID_ADDRESS   96UL
#define ARM64_SYS_IOCTL             29UL
#define ARM64_SYS_OPENAT            56UL
#define ARM64_SYS_PPOLL             73UL
#define ARM64_SYS_EXIT_GROUP        94UL
#define ARM64_SYS_RT_SIGACTION     134UL
#define ARM64_SYS_RT_SIGPROCMASK   135UL
#define ARM64_SYS_PRCTL            167UL
#define ARM64_SYS_PRLIMIT64        261UL
#define ARM64_SYS_RSEQ             293UL
#define ARM64_SYS_GETRANDOM        278UL
#define ARM64_SYS_BRK              214UL
#define ARM64_SYS_MUNMAP           215UL
#define ARM64_SYS_MMAP             222UL
#define ARM64_SYS_MPROTECT         226UL
#define ARM64_SYS_GETUID           174UL
#define ARM64_SYS_GETEUID          175UL
#define ARM64_SYS_GETGID           176UL
#define ARM64_SYS_GETEGID          177UL
#define ARM64_SYS_GETPPID          173UL
#define ARM64_SYS_FCNTL             25UL
#define ARM64_SYS_READ              63UL
#define ARM64_SYS_CLOSE             57UL
#define ARM64_SYS_NEWFSTATAT        79UL
#define ARM64_SYS_FSTAT             80UL
#define ARM64_SYS_GETDENTS64        61UL
#define ARM64_SYS_DUP               23UL
#define ARM64_SYS_DUP3              24UL
#define ARM64_SYS_PIPE2             59UL
#define ARM64_SYS_CLONE            220UL
#define ARM64_SYS_EXECVE           221UL
#define ARM64_SYS_WAIT4            260UL
#define ARM64_SYS_UNAME            160UL
#define ARM64_SYS_GETCWD            17UL
#define ARM64_SYS_CHDIR             49UL
#define ARM64_SYS_FACCESSAT         48UL
#define ARM64_SYS_SET_ROBUST_LIST  273UL
#define ARM64_SYS_CLOCK_GETRES     114UL

#define ENOTTY 25

#define MUSL_MMAP_BASE 0x431a0000UL
#define MUSL_MMAP_END  0x43200000UL
/*
 * BusyBox data LOAD ends ~0x44157b60; heap brk starts at next page.
 * Anonymous mmap bump shares the same high window up to BB_MMAP_END.
 */
#define BB_BRK_START   0x44158000UL
#define BB_MMAP_BASE   0x44200000UL
#define BB_MMAP_END    0x44800000UL

static uint64_t g_musl_brk = MUSL_MMAP_BASE;
static uint64_t g_musl_mmap_bump = MUSL_MMAP_BASE;
static uint64_t g_bb_brk = BB_BRK_START;
static uint64_t g_bb_mmap_bump = BB_MMAP_BASE;

void arm64_syscall_reset_busybox_heap(void)
{
	g_bb_brk = BB_BRK_START;
	g_bb_mmap_bump = BB_MMAP_BASE;
}

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

static void copy_uname_field(char *dst, const char *src)
{
	unsigned i;

	for (i = 0; i < 64 && src[i]; i++)
		dst[i] = src[i];
	for (; i < 65; i++)
		dst[i] = 0;
}

static void pl011_put_hex64(uint64_t v)
{
	static const char hex[] = "0123456789abcdef";
	char buf[17];
	int i;

	for (i = 15; i >= 0; i--)
	{
		buf[i] = hex[v & 0xfUL];
		v >>= 4;
	}
	buf[16] = 0;
	pl011_puts(buf);
}

static void zero_page(uint64_t page)
{
	volatile uint8_t *p = (volatile uint8_t *)(uintptr_t)page;
	uint64_t i;

	for (i = 0; i < 4096UL; i++)
		p[i] = 0;
}

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

	if (fd != 1UL && fd != 2UL)
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
	if (arm64_musl_mode())
		arm64_musl_note_write(p, n);
	if (arm64_busybox_mode())
		arm64_busybox_note_write(p, n);
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
	case ARM64_SYS_GETTID:
		return 1;
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
	case ARM64_SYS_SET_TID_ADDRESS:
		return 1;
	case ARM64_SYS_GETUID:
	case ARM64_SYS_GETEUID:
	case ARM64_SYS_GETGID:
	case ARM64_SYS_GETEGID:
		return 0;
	case ARM64_SYS_GETPPID:
		return 1;
	case ARM64_SYS_IOCTL:
		/* isatty / TCGETS → not a tty */
		return -ENOTTY;
	case ARM64_SYS_FCNTL:
		return 0;
	case ARM64_SYS_READ:
	{
		int64_t rr = arm64_rootfs_read((int)a0, a1, a2);

		if (rr != -EBADF)
			return rr;
		return 0;
	}
	case ARM64_SYS_CLOSE:
	{
		int64_t cr = arm64_rootfs_close((int)a0);

		if (cr != -EBADF)
			return cr;
		return 0;
	}
	case ARM64_SYS_OPENAT:
	{
		int64_t or = arm64_rootfs_openat((int)a0, a1, (int)a2);

		if (or != -ENOENT)
			return or;
		return -ENOENT;
	}
	case ARM64_SYS_FACCESSAT:
	{
		int64_t ar = arm64_rootfs_faccessat((int)a0, a1, (int)a2);

		if (ar != -ENOENT)
			return ar;
		return -ENOENT;
	}
	case ARM64_SYS_NEWFSTATAT:
	{
		int64_t sr = arm64_rootfs_newfstatat((int)a0, a1, a2, (int)a3);

		if (sr != -ENOENT)
			return sr;
		return -ENOENT;
	}
	case ARM64_SYS_FSTAT:
	{
		int64_t fr = arm64_rootfs_fstat((int)a0, a1);

		if (fr != -EBADF)
			return fr;
		return -ENOENT;
	}
	case ARM64_SYS_GETDENTS64:
		return -ENOSYS;
	case ARM64_SYS_DUP:
		return (int64_t)a0;
	case ARM64_SYS_DUP3:
		return (int64_t)a1;
	case ARM64_SYS_PIPE2:
	case ARM64_SYS_CLONE:
	case ARM64_SYS_EXECVE:
	case ARM64_SYS_WAIT4:
		return -ENOSYS;
	case ARM64_SYS_UNAME:
		if (arm64_mmu_user_buf_ok(a0, 390))
		{
			char *u = (char *)(uintptr_t)a0;
			unsigned i;

			for (i = 0; i < 390; i++)
				u[i] = 0;
			copy_uname_field(u + 0, "Linux");
			copy_uname_field(u + 65, "ir0");
			copy_uname_field(u + 130, "0.0.1");
			copy_uname_field(u + 260, "aarch64");
			return 0;
		}
		return -EFAULT;
	case ARM64_SYS_GETCWD:
		if (a1 >= 2 && arm64_mmu_user_buf_ok(a0, a1))
		{
			char *p = (char *)(uintptr_t)a0;

			p[0] = '/';
			p[1] = 0;
			return 2;
		}
		return -EFAULT;
	case ARM64_SYS_CHDIR:
		return -ENOENT;
	case ARM64_SYS_SET_ROBUST_LIST:
	case ARM64_SYS_CLOCK_GETRES:
		return 0;
	case ARM64_SYS_PPOLL:
		return 0;
	case ARM64_SYS_RT_SIGACTION:
	case ARM64_SYS_RT_SIGPROCMASK:
		return 0;
	case ARM64_SYS_PRCTL:
		return 0;
	case ARM64_SYS_PRLIMIT64:
		/* Fill old_rlim with RLIM_INFINITY so musl malloc isn't capped at 0. */
		if (a3 != 0 && arm64_mmu_user_buf_ok(a3, 16))
		{
			uint64_t *rlim = (uint64_t *)(uintptr_t)a3;

			rlim[0] = ~0ULL;
			rlim[1] = ~0ULL;
		}
		return 0;
	case ARM64_SYS_RSEQ:
		return 0;
	case ARM64_SYS_GETRANDOM:
		if (a1 > 0 && arm64_mmu_user_buf_ok(a0, a1 > 64 ? 64 : a1))
		{
			uint8_t *p = (uint8_t *)(uintptr_t)a0;
			uint64_t n = a1 > 64 ? 64 : a1;
			uint64_t i;

			for (i = 0; i < n; i++)
				p[i] = (uint8_t)(i + 1);
			return (int64_t)n;
		}
		return -EFAULT;
	case ARM64_SYS_BRK:
	{
		uint64_t req = a0;
		uint64_t *brk;
		uint64_t base;
		uint64_t end;

		if (arm64_busybox_mode())
		{
			brk = &g_bb_brk;
			base = BB_BRK_START;
			end = BB_MMAP_END;
		}
		else
		{
			brk = &g_musl_brk;
			base = MUSL_MMAP_BASE;
			end = MUSL_MMAP_END;
		}
		if (req == 0)
			return (int64_t)*brk;
		if (req < base || req > end)
			return (int64_t)*brk;
		while (*brk < req)
		{
			uint64_t page = *brk & ~(4096UL - 1UL);

			if (arm64_mmu_map_user_page_flags(page, 0) != 0)
				return (int64_t)*brk;
			zero_page(page);
			*brk += 4096UL;
		}
		*brk = req;
		return (int64_t)*brk;
	}
	case ARM64_SYS_MMAP:
	{
		uint64_t addr = a0;
		uint64_t len = a1;
		uint64_t page;
		uint64_t base;
		uint64_t *bump;
		uint64_t mend;
		uint64_t map_end;

		if (len == 0)
		{
			/* musl mallocng can issue mmap(0,0) if page_size was 0; give 2 pages. */
			len = 8192UL;
		}
		len = (len + 4095UL) & ~4095UL;
		if (arm64_busybox_mode())
		{
			bump = &g_bb_mmap_bump;
			mend = BB_MMAP_END;
		}
		else
		{
			bump = &g_musl_mmap_bump;
			mend = MUSL_MMAP_END;
		}
		/* MAP_FIXED / hint: honour non-zero addr. */
		if (addr != 0)
		{
			base = addr & ~(4096UL - 1UL);
			map_end = base + len;
			if (map_end < base)
				return -ENOMEM;
			for (page = base; page < map_end; page += 4096UL)
			{
				if (arm64_mmu_map_user_page_flags(page, 0) != 0)
					return -ENOMEM;
				zero_page(page);
			}
			if (map_end > *bump && map_end <= mend)
				*bump = map_end;
			return (int64_t)base;
		}
		if (*bump + len > mend)
			return -ENOMEM;
		base = *bump;
		for (page = base; page < base + len; page += 4096UL)
		{
			if (arm64_mmu_map_user_page_flags(page, 0) != 0)
				return -ENOMEM;
			zero_page(page);
		}
		*bump = base + len;
		return (int64_t)base;
	}
	case ARM64_SYS_MUNMAP:
	case ARM64_SYS_MPROTECT:
		return 0;
	case ARM64_SYS_EXIT:
	case ARM64_SYS_EXIT_GROUP:
		if (leave_el0)
		{
			*leave_el0 = 1;
		}
		if (arm64_musl_mode() || arm64_busybox_mode())
			return a0;
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
		if (arm64_busybox_mode())
		{
			pl011_puts("ARM64_BB_ENOSYS_");
			pl011_put_hex64(nr);
			pl011_puts("\n");
		}
		return -ENOSYS;
	}
}
