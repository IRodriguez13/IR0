/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: cases_procfs.c
 * Description: Invariants behind what top, ps, free, uptime and vmstat show:
 *              /proc and /sys figures must agree with the clock, with each
 *              other and with the syscalls that report the same quantity.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "conf.h"

#include <sys/mount.h>
#include <sys/resource.h>
#include <sys/sysinfo.h>
#include <sys/times.h>
#include <sys/utsname.h>

/* USER_HZ: the unit of every clock_t in /proc and times(2) on Linux. */
#define USER_HZ 100L

static char g_buf[8192];

/* Mount proc and sysfs when the guest init did not (Linux rdinit=/init). */
static void ensure_mount(const char *dir, const char *probe, const char *type)
{
	long fd = S4(SYS_openat, AT_FDCWD, probe, O_RDONLY, 0);

	if (fd >= 0)
	{
		(void)S1(SYS_close, fd);
		return;
	}
	(void)S3(SYS_mkdirat, AT_FDCWD, dir, 0555);
	(void)S5(SYS_mount, type, dir, type, 0, 0);
}

static void ensure_proc(void)
{
	ensure_mount("/proc", "/proc/stat", "proc");
}

/* Read a whole small file into g_buf; length or -errno. */
static long slurp(const char *path)
{
	long fd = S4(SYS_openat, AT_FDCWD, path, O_RDONLY, 0);
	long len = 0;

	if (fd < 0)
		return fd;
	for (;;)
	{
		long n = S3(SYS_read, fd, g_buf + len,
			    (long)sizeof(g_buf) - 1 - len);

		if (n <= 0)
			break;
		len += n;
		if (len >= (long)sizeof(g_buf) - 1)
			break;
	}
	(void)S1(SYS_close, fd);
	g_buf[len] = '\0';
	return len;
}

static int is_digit(char c)
{
	return c >= '0' && c <= '9';
}

static int is_space(char c)
{
	return c == ' ' || c == '\t' || c == '\n';
}

/* Decimal number at p, optionally signed; *end gets the first other byte. */
static long parse_num(const char *p, const char **end)
{
	long sign = 1;
	long v = 0;

	while (*p == ' ' || *p == '\t')
		p++;
	if (*p == '-')
	{
		sign = -1;
		p++;
	}
	while (is_digit(*p))
		v = v * 10 + (*p++ - '0');
	if (end)
		*end = p;
	return sign * v;
}

/* "12.34" -> 1234 (hundredths), as /proc/uptime and /proc/loadavg print. */
static long parse_centi(const char *p, const char **end)
{
	const char *q;
	long whole = parse_num(p, &q);
	long frac = 0;

	if (*q == '.')
	{
		q++;
		if (is_digit(*q))
			frac = (*q++ - '0') * 10;
		if (is_digit(*q))
			frac += *q++ - '0';
		while (is_digit(*q))
			q++;
	}
	if (end)
		*end = q;
	return whole * 100 + frac;
}

/* Start of the line that begins with key, or NULL. */
static const char *find_line(const char *buf, const char *key)
{
	size_t klen = rt_strlen(key);
	const char *p = buf;

	while (*p)
	{
		if (rt_memeq(p, key, klen))
			return p + klen;
		while (*p && *p != '\n')
			p++;
		if (*p)
			p++;
	}
	return 0;
}

/* Number after key ("MemTotal:", "processes "), or -1 when absent. */
static long key_num(const char *buf, const char *key)
{
	const char *p = find_line(buf, key);

	return p ? parse_num(p, 0) : -1;
}

/* Uptime in hundredths of a second (USER_HZ ticks). */
static long uptime_centi(void)
{
	if (slurp("/proc/uptime") <= 0)
		return -1;
	return parse_centi(g_buf, 0);
}

/* /proc/stat: the aggregate cpu line summed, and the cpuN line count. */
static long stat_cpu_sum(long *ncpu)
{
	const char *p;
	long sum = 0;
	int i;

	*ncpu = 0;
	if (slurp("/proc/stat") <= 0)
		return -1;
	p = find_line(g_buf, "cpu ");
	if (!p)
		return -1;
	for (i = 0; i < 8; i++)
		sum += parse_num(p, &p);
	for (p = g_buf; *p; p++)
	{
		if ((p == g_buf || p[-1] == '\n') && rt_memeq(p, "cpu", 3) &&
		    is_digit(p[3]))
			(*ncpu)++;
	}
	return sum;
}

/*
 * Field n (1-based) of /proc/<pid>/stat. The comm in field 2 may hold
 * spaces, so counting restarts after its closing parenthesis.
 */
static long stat_field(const char *line, int n)
{
	const char *p = line;
	const char *close = 0;
	int f;

	for (; *p; p++)
		if (*p == ')')
			close = p;
	if (!close || n < 3)
		return -1;
	p = close + 1;
	for (f = 3; f < n; f++)
	{
		while (is_space(*p))
			p++;
		while (*p && !is_space(*p))
			p++;
	}
	return parse_num(p, 0);
}

static int stat_field_count(const char *line)
{
	const char *p = line;
	const char *close = 0;
	int n = 2;

	for (; *p; p++)
		if (*p == ')')
			close = p;
	if (!close)
		return -1;
	for (p = close + 1; *p;)
	{
		while (is_space(*p))
			p++;
		if (!*p)
			break;
		n++;
		while (*p && !is_space(*p))
			p++;
	}
	return n;
}

static void pid_path(char *out, size_t cap, long pid, const char *leaf)
{
	out[0] = '\0';
	rt_cat(out, cap, "/proc/");
	rt_catn(out, cap, pid);
	rt_cat(out, cap, leaf);
}

static void spin_ms(long ms)
{
	long start = rt_now_ms();

	while (rt_now_ms() - start < ms)
		;
}

/* The cpu jiffies of /proc/stat add up to uptime * USER_HZ per CPU. */
int procfs_cpu_jiffies_vs_uptime(void)
{
	long up0, up1, sum, ncpu, want;

	ensure_proc();
	OK(up0, uptime_centi());
	OK(sum, stat_cpu_sum(&ncpu));
	OK(up1, uptime_centi());
	CHECK(ncpu >= 1);
	want = (up0 + up1) / 2 * ncpu;
	rt_diag("cpu_sum=", sum);
	rt_diag("uptime_x_ncpu=", want);
	CHECK(sum * 10 >= want * 9 - 100 * ncpu);
	CHECK(sum * 10 <= want * 11 + 100 * ncpu);
	return CONF_PASS;
}

/*
 * A task that spins for a known wall time is charged that much CPU, no
 * more, in /proc/<pid>/stat, in times(2) and in getrusage(2) once reaped;
 * its starttime is the uptime at fork in USER_HZ.
 */
int procfs_task_cpu_times(void)
{
	char path[64];
	int done[2];
	long up_fork, t0, wall, pid, n, ticks, start, rus_ms;
	struct tms tms;
	struct rusage ru;
	char c;
	int status;

	ensure_proc();
	OK(n, S2(SYS_pipe2, done, 0));
	OK(up_fork, uptime_centi());
	t0 = rt_now_ms();
	pid = rt_fork();
	if (pid == 0)
	{
		spin_ms(400);
		(void)S3(SYS_write, done[1], "x", 1);
		for (;;)
			rt_sleep_ms(1000);
	}
	CHECK(pid > 0);
	EXPECT(S3(SYS_read, done[0], &c, 1), 1);
	wall = rt_now_ms() - t0;
	rt_sleep_ms(30);
	pid_path(path, sizeof(path), pid, "/stat");
	OK(n, slurp(path));
	ticks = stat_field(g_buf, 14) + stat_field(g_buf, 15);
	start = stat_field(g_buf, 22);
	rt_diag("wall_ms=", wall);
	rt_diag("utime_stime=", ticks);
	rt_diag("starttime=", start);
	rt_diag("uptime_at_fork=", up_fork);
	CHECK(ticks * 1000 <= (wall + 40) * USER_HZ);
	CHECK(ticks * 1000 >= wall * USER_HZ / 3);
	CHECK(start >= up_fork - 5 && start <= up_fork + 30);
	EXPECT(S2(SYS_kill, pid, SIGKILL), 0);
	EXPECT(rt_wait(pid, &status, 0), pid);
	OK(n, S1(SYS_times, &tms));
	rt_diag("cutime_cstime=", (long)(tms.tms_cutime + tms.tms_cstime));
	CHECK((long)(tms.tms_cutime + tms.tms_cstime) >= ticks);
	CHECK((long)(tms.tms_cutime + tms.tms_cstime) <= ticks + 10);
	EXPECT(S2(SYS_getrusage, RUSAGE_CHILDREN, &ru), 0);
	rus_ms = (long)(ru.ru_utime.tv_sec + ru.ru_stime.tv_sec) * 1000 +
		 (long)(ru.ru_utime.tv_usec + ru.ru_stime.tv_usec) / 1000;
	rt_diag("rusage_children_ms=", rus_ms);
	CHECK(rus_ms >= ticks * 10 - 10 && rus_ms <= ticks * 10 + 110);
	return CONF_PASS;
}

/* "processes" in /proc/stat counts forks since boot, not live tasks. */
int procfs_stat_processes_forks(void)
{
	long p0, p1, pid;
	int status, i;

	ensure_proc();
	OK(p0, slurp("/proc/stat"));
	p0 = key_num(g_buf, "processes ");
	CHECK(p0 > 0);
	for (i = 0; i < 3; i++)
	{
		pid = rt_fork();
		if (pid == 0)
			rt_exit(0);
		EXPECT(rt_wait(pid, &status, 0), pid);
	}
	OK(p1, slurp("/proc/stat"));
	p1 = key_num(g_buf, "processes ");
	rt_diag("processes_before=", p0);
	rt_diag("processes_after=", p1);
	CHECK(p1 >= p0 + 3);
	return CONF_PASS;
}

/* free(1) and sysinfo(2) see the same memory: MemTotal, MemFree. */
int procfs_meminfo_vs_sysinfo(void)
{
	struct sysinfo si;
	long total, mfree, si_total, si_free, n;

	ensure_proc();
	EXPECT(S1(SYS_sysinfo, &si), 0);
	OK(n, slurp("/proc/meminfo"));
	total = key_num(g_buf, "MemTotal:");
	mfree = key_num(g_buf, "MemFree:");
	si_total = (long)((unsigned long)si.totalram * si.mem_unit / 1024);
	si_free = (long)((unsigned long)si.freeram * si.mem_unit / 1024);
	rt_diag("MemTotal=", total);
	rt_diag("sysinfo_total=", si_total);
	rt_diag("used_meminfo=", total - mfree);
	rt_diag("used_sysinfo=", si_total - si_free);
	CHECK(total > 0 && total == si_total);
	CHECK(mfree > 0 && mfree <= total);
	/* Used memory agrees within 2% of RAM between the two reads. */
	CHECK((total - mfree) - (si_total - si_free) <= total / 50);
	CHECK((si_total - si_free) - (total - mfree) <= total / 50);
	CHECK(key_num(g_buf, "MemAvailable:") >= 0);
	return CONF_PASS;
}

/* SwapTotal/SwapFree agree with sysinfo(2) and with /proc/swaps. */
int procfs_swap_consistent(void)
{
	struct sysinfo si;
	long swap_total, swap_free, n, sum_size = 0, sum_used = 0;
	const char *p;

	ensure_proc();
	EXPECT(S1(SYS_sysinfo, &si), 0);
	OK(n, slurp("/proc/meminfo"));
	swap_total = key_num(g_buf, "SwapTotal:");
	swap_free = key_num(g_buf, "SwapFree:");
	rt_diag("SwapTotal=", swap_total);
	rt_diag("SwapFree=", swap_free);
	CHECK(swap_total >= 0 && swap_free >= 0 && swap_free <= swap_total);
	EXPECT(swap_total,
	       (long)((unsigned long)si.totalswap * si.mem_unit / 1024));
	EXPECT(swap_free,
	       (long)((unsigned long)si.freeswap * si.mem_unit / 1024));
	OK(n, slurp("/proc/swaps"));
	/* Header line, then: Filename Type Size Used Priority (kB). */
	for (p = g_buf; *p && *p != '\n'; p++)
		;
	while (*p)
	{
		int f;

		p++;
		if (!*p)
			break;
		for (f = 0; f < 2; f++)
		{
			while (*p && !is_space(*p))
				p++;
			while (*p == ' ' || *p == '\t')
				p++;
		}
		sum_size += parse_num(p, &p);
		sum_used += parse_num(p, &p);
		while (*p && *p != '\n')
			p++;
	}
	EXPECT(sum_size, swap_total);
	EXPECT(sum_used, swap_total - swap_free);
	return CONF_PASS;
}

/*
 * N spinning tasks show up in procs_running and in the run count of
 * /proc/loadavg at once, raise load1 over the next LOAD_FREQ periods
 * (5 s each), and load1 decays once they are gone.
 */
#define LOAD_SPINNERS 4

int procfs_loadavg_runqueue(void)
{
	long pids[LOAD_SPINNERS];
	long l0, l1, l2, running, r_load, n;
	const char *p;
	int i, status;

	ensure_proc();
	OK(n, slurp("/proc/loadavg"));
	l0 = parse_centi(g_buf, 0);
	for (i = 0; i < LOAD_SPINNERS; i++)
	{
		/* Bounded, so a failed case never leaves a spinner behind. */
		pids[i] = rt_fork();
		if (pids[i] == 0)
		{
			spin_ms(12000);
			rt_exit(0);
		}
	}
	rt_sleep_ms(500);
	OK(n, slurp("/proc/stat"));
	running = key_num(g_buf, "procs_running ");
	OK(n, slurp("/proc/loadavg"));
	p = g_buf;
	for (i = 0; i < 3; i++)
		(void)parse_centi(p, &p);
	r_load = parse_num(p, 0);
	rt_diag("procs_running=", running);
	rt_diag("loadavg_running=", r_load);
	rt_sleep_ms(10600);
	OK(n, slurp("/proc/loadavg"));
	l1 = parse_centi(g_buf, 0);
	for (i = 0; i < LOAD_SPINNERS; i++)
	{
		(void)S2(SYS_kill, pids[i], SIGKILL);
		(void)rt_wait(pids[i], &status, 0);
	}
	rt_sleep_ms(10600);
	OK(n, slurp("/proc/loadavg"));
	l2 = parse_centi(g_buf, 0);
	rt_diag("load1_before_x100=", l0);
	rt_diag("load1_spinning_x100=", l1);
	rt_diag("load1_after_x100=", l2);
	for (i = 0; i < LOAD_SPINNERS; i++)
		CHECK(pids[i] > 0);
	CHECK(running >= LOAD_SPINNERS);
	CHECK(r_load >= LOAD_SPINNERS);
	CHECK(l1 >= l0 + 40);
	CHECK(l2 < l1);
	return CONF_PASS;
}

/* Parse "0" or "0-N" (a sysfs cpu list); the count of CPUs or -1. */
static long cpu_list_count(const char *path)
{
	const char *p;
	long lo, hi;

	if (slurp(path) <= 0)
		return -1;
	lo = parse_num(g_buf, &p);
	hi = lo;
	if (*p == '-')
		hi = parse_num(p + 1, &p);
	if (*p != '\n' || lo != 0 || hi < lo)
		return -1;
	return hi - lo + 1;
}

/* online/possible/present name the CPUs /proc/stat lists. */
int sysfs_cpu_lists(void)
{
	long ncpu, online, possible, present, n;

	ensure_proc();
	ensure_mount("/sys", "/sys/devices/system/cpu/online", "sysfs");
	OK(n, stat_cpu_sum(&ncpu));
	online = cpu_list_count("/sys/devices/system/cpu/online");
	possible = cpu_list_count("/sys/devices/system/cpu/possible");
	present = cpu_list_count("/sys/devices/system/cpu/present");
	rt_diag("stat_ncpu=", ncpu);
	rt_diag("online=", online);
	EXPECT(online, ncpu);
	CHECK(present >= online);
	CHECK(possible >= present);
	return CONF_PASS;
}

/*
 * /proc/<pid>/stat has the 52 fields of Linux; nice (19) follows
 * setpriority(2), priority (18) is 20 + nice and num_threads (20) counts
 * the thread group.
 */
int procfs_pid_stat_fields(void)
{
	long n;

	ensure_proc();
	OK(n, slurp("/proc/self/stat"));
	rt_diag("fields=", stat_field_count(g_buf));
	EXPECT(stat_field_count(g_buf), 52);
	EXPECT(stat_field(g_buf, 20), 1);
	EXPECT(S3(SYS_setpriority, PRIO_PROCESS, 0, 5), 0);
	OK(n, slurp("/proc/self/stat"));
	EXPECT(stat_field(g_buf, 19), 5);
	EXPECT(stat_field(g_buf, 18), 25);
	return CONF_PASS;
}

/* status carries Tgid, VmRSS and the context switch counters. */
int procfs_pid_status_keys(void)
{
	long n, pid = S0(SYS_getpid);

	ensure_proc();
	OK(n, slurp("/proc/self/status"));
	EXPECT(key_num(g_buf, "Tgid:"), pid);
	CHECK(key_num(g_buf, "VmSize:") > 0);
	CHECK(key_num(g_buf, "VmRSS:") > 0);
	CHECK(key_num(g_buf, "RssAnon:") >= 0);
	CHECK(key_num(g_buf, "voluntary_ctxt_switches:") >= 0);
	CHECK(key_num(g_buf, "nonvoluntary_ctxt_switches:") >= 0);
	return CONF_PASS;
}

/* /proc/<pid>/task/<tid>/stat names the thread itself. */
int procfs_task_dir(void)
{
	char path[64];
	long n, pid = S0(SYS_getpid);

	ensure_proc();
	path[0] = '\0';
	rt_cat(path, sizeof(path), "/proc/self/task/");
	rt_catn(path, sizeof(path), pid);
	rt_cat(path, sizeof(path), "/stat");
	OK(n, slurp(path));
	EXPECT(parse_num(g_buf, 0), pid);
	EXPECT(stat_field_count(g_buf), 52);
	return CONF_PASS;
}

/* /proc/sys/kernel and /proc/vmstat, as procps and glibc read them. */
int procfs_sys_kernel_vmstat(void)
{
	struct utsname u;
	long n;
	size_t len;

	ensure_proc();
	OK(n, slurp("/proc/sys/kernel/pid_max"));
	CHECK(parse_num(g_buf, 0) > 1);
	EXPECT(S1(SYS_uname, &u), 0);
	OK(n, slurp("/proc/sys/kernel/osrelease"));
	len = rt_strlen(u.release);
	CHECK(rt_memeq(g_buf, u.release, len) && g_buf[len] == '\n');
	OK(n, slurp("/proc/vmstat"));
	CHECK(key_num(g_buf, "nr_free_pages ") > 0);
	CHECK(key_num(g_buf, "pswpin ") >= 0);
	CHECK(key_num(g_buf, "pswpout ") >= 0);
	return CONF_PASS;
}

/* cpuinfo: one "processor\t: N" block per CPU in "key\t: value" form. */
int procfs_cpuinfo_format(void)
{
	long n, ncpu, blocks = 0;
	const char *p;

	ensure_proc();
	OK(n, stat_cpu_sum(&ncpu));
	OK(n, slurp("/proc/cpuinfo"));
	for (p = g_buf; *p; p++)
	{
		if ((p == g_buf || p[-1] == '\n') &&
		    rt_memeq(p, "processor\t: ", 12))
			blocks++;
	}
	rt_diag("processor_blocks=", blocks);
	EXPECT(blocks, ncpu);
	return CONF_PASS;
}
