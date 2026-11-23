/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: sched_nice.c
 * Description: Two CPU-bound children with different nice values
 *              must not receive the same user CPU time.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#endif

#include "ir0/ksft.h"

#ifdef IR0_SELFTEST_NOLIBC
/* arch/arm64/include/uapi/asm/unistd.h: __NR_setpriority is 140. */
#define SYS_setpriority 140
#define PRIO_PROCESS 0
#endif

/*
 * proc(5): utime is field 14 of /proc/<pid>/stat, after flags, minflt,
 * cminflt, majflt and cmajflt (fields 9-13).
 */
#define STAT_UTIME_FIELD 14

/* 1000 Hz tick, 10 ms quantum: 200 ms is twenty quanta. */
#define SAMPLE_NSEC 200000000L

static void spin_forever(void)
{
	volatile unsigned long counter;

	counter = 0;
	for (;;)
	{
		counter++;
	}
}

static int set_nice(int pid, int nice)
{
#ifdef IR0_SELFTEST_NOLIBC
	return (int)syscall(SYS_setpriority, PRIO_PROCESS, pid, nice);
#else
	return setpriority(PRIO_PROCESS, pid, nice);
#endif
}

static int wait_child(int pid, int *status)
{
#ifdef IR0_SELFTEST_NOLIBC
	return wait4(pid, status, 0, 0);
#else
	return waitpid(pid, status, 0);
#endif
}

static void kill_and_reap(int pid)
{
	int status;

	if (pid <= 0)
	{
		return;
	}
	(void)kill(pid, SIGKILL);
	(void)wait_child(pid, &status);
}

static void sleep_sample(void)
{
	struct timespec req;

	req.tv_sec = 0;
	req.tv_nsec = SAMPLE_NSEC;
	(void)nanosleep(&req, 0);
}

static void format_proc_stat(char *dst, int pid)
{
	const char *prefix = "/proc/";
	const char *suffix = "/stat";
	char digits[16];
	unsigned int mag;
	unsigned int n;
	int k;

	k = 0;
	while (prefix[k] != 0)
	{
		dst[k] = prefix[k];
		k++;
	}
	mag = (unsigned int)pid;
	n = 0;
	if (mag == 0)
	{
		digits[n++] = '0';
	}
	while (mag != 0 && n < sizeof(digits))
	{
		digits[n++] = (char)('0' + (mag % 10U));
		mag /= 10U;
	}
	while (n > 0)
	{
		n--;
		dst[k++] = digits[n];
	}
	n = 0;
	while (suffix[n] != 0)
	{
		dst[k++] = suffix[n];
		n++;
	}
	dst[k] = 0;
}

/* Field numbers after the closing ')' of comm start at 3. */
static int stat_field_u64(const char *buf, int want, unsigned long long *out)
{
	const char *p;
	const char *last;
	int field;
	unsigned long long value;

	last = 0;
	p = buf;
	while (*p != 0)
	{
		if (*p == ')')
		{
			last = p;
		}
		p++;
	}
	if (last == 0)
	{
		return -1;
	}
	p = last + 1;
	field = 3;
	while (*p != 0)
	{
		while (*p == ' ' || *p == '\t' || *p == '\n')
		{
			p++;
		}
		if (*p == 0)
		{
			break;
		}
		if (field == want)
		{
			value = 0;
			if (*p < '0' || *p > '9')
			{
				return -1;
			}
			while (*p >= '0' && *p <= '9')
			{
				value = value * 10ULL + (unsigned long long)(*p - '0');
				p++;
			}
			*out = value;
			return 0;
		}
		while (*p != 0 && *p != ' ' && *p != '\t' && *p != '\n')
		{
			p++;
		}
		field++;
	}
	return -1;
}

static int read_utime(int pid, unsigned long long *utime)
{
	char path[32];
	char buf[512];
	volatile unsigned char *z;
	int fd;
	int n;
	int i;

	format_proc_stat(path, pid);
	fd = open(path, 0);
	if (fd < 0)
	{
		return -1;
	}
	z = (volatile unsigned char *)buf;
	for (i = 0; i < (int)sizeof(buf); i++)
	{
		z[i] = 0;
	}
	n = (int)read(fd, buf, sizeof(buf) - 1);
	(void)close(fd);
	if (n < 0)
	{
		return -1;
	}
	buf[n] = 0;
	return stat_field_u64(buf, STAT_UTIME_FIELD, utime);
}

int main(void)
{
	int nice0;
	int nice10;
	unsigned long long utime0;
	unsigned long long utime10;
	int pass;

	ksft_set_plan(1);
	nice0 = -1;
	nice10 = -1;
	pass = 0;

	nice0 = fork();
	if (nice0 < 0)
	{
		goto out;
	}
	if (nice0 == 0)
	{
		spin_forever();
		_exit(0);
	}

	nice10 = fork();
	if (nice10 < 0)
	{
		goto out;
	}
	if (nice10 == 0)
	{
		spin_forever();
		_exit(0);
	}

	if (set_nice(nice0, 0) != 0 || set_nice(nice10, 10) != 0)
	{
		goto out;
	}

	sleep_sample();

	if (read_utime(nice0, &utime0) != 0 || read_utime(nice10, &utime10) != 0)
	{
		goto out;
	}
	ksft_print_num("utime_nice0", (long)utime0);
	ksft_print_num("utime_nice10", (long)utime10);
	pass = utime0 > utime10;

out:
	kill_and_reap(nice0);
	kill_and_reap(nice10);
	ksft_test_result(pass, "sched_nice");
	return ksft_exit();
}
