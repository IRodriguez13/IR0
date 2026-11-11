/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: conformance.c
 * Description: Guest-side conformance runner (/sbin/init). Runs every case in
 *              cases.h in its own child and working directory, then the LTP
 *              binaries listed in /ltp/manifest when the image carries them,
 *              and prints one KTAP line per case:
 *
 *                ok N name                      PASS
 *                not ok N name                  FAIL
 *                ok N name # SKIP TCONF ...     not supported (TCONF)
 *                not ok N name # ERROR BROK ... crashed, hung, or bad exit
 *
 *              The host side (scripts/ir0_conformance.py) compares the
 *              result against the checked-in baseline.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "conf.h"

#include <dirent.h>
#include <sys/stat.h>

#define CASE_TIMEOUT_MS 10000L
#define LTP_TIMEOUT_MS 60000L
#define LTP_MAX 128
#define LTP_NAME_MAX 48
#define RESULT_BROK 3

/* LTP exit status bits (include/tst_res_flags.h). */
#define LTP_TFAIL 1
#define LTP_TBROK 2
#define LTP_TCONF 32

struct conf_entry
{
	const char *name;
	int (*fn)(void);
};

#undef CONF_CASE
#define CONF_CASE(name) { #name, name },
static const struct conf_entry g_cases[] = {
#include CONF_CASE_LIST
};
#define NCASES (sizeof(g_cases) / sizeof(g_cases[0]))

const char *conf_self = "/sbin/init";
int conf_guest;
static char g_base[128];
static char g_ltp_names[LTP_MAX][LTP_NAME_MAX];
static char g_ltp_file[LTP_MAX * LTP_NAME_MAX];
static unsigned int g_nltp;
static unsigned int g_count[4];
static unsigned int g_index;

static void line_begin(char *line, size_t cap, int ok, const char *prefix,
		       const char *name)
{
	line[0] = '\0';
	rt_cat(line, cap, ok ? "ok " : "not ok ");
	rt_catn(line, cap, (long)++g_index);
	rt_cat(line, cap, " ");
	rt_cat(line, cap, prefix);
	rt_cat(line, cap, name);
}

/* result: CONF_PASS/FAIL/TCONF or RESULT_BROK with why/val as detail. */
static void report(const char *prefix, const char *name, int result,
		   const char *why, long val)
{
	char line[192];

	line_begin(line, sizeof(line), result == CONF_PASS || result == CONF_TCONF,
		   prefix, name);
	if (result == CONF_TCONF)
	{
		rt_cat(line, sizeof(line), " # SKIP TCONF");
	}
	else if (result == RESULT_BROK)
	{
		rt_cat(line, sizeof(line), " # ERROR BROK ");
		rt_cat(line, sizeof(line), why);
		rt_catn(line, sizeof(line), val);
	}
	rt_cat(line, sizeof(line), "\n");
	rt_puts(line);
	g_count[result]++;
}

int conf_check(long got, long want, const char *what, int line)
{
	if (got == want)
		return CONF_PASS;
	rt_puts("# at line ");
	rt_putn(line);
	rt_puts(": ");
	rt_puts(what);
	rt_puts("\n");
	rt_diag("got", got);
	rt_diag("want", want);
	if (got == -ENOSYS)
		return CONF_TCONF;
	return CONF_FAIL;
}

int conf_check_ok(long got, const char *what, int line)
{
	if (got >= 0 || (got < -4095 && got != -ENOSYS))
		return CONF_PASS;
	rt_puts("# at line ");
	rt_putn(line);
	rt_puts(": ");
	rt_puts(what);
	rt_puts("\n");
	rt_diag("errno", -got);
	return got == -ENOSYS ? CONF_TCONF : CONF_FAIL;
}

int conf_check_cond(int cond, const char *what, int line)
{
	if (cond)
		return CONF_PASS;
	rt_puts("# at line ");
	rt_putn(line);
	rt_puts(": CHECK(");
	rt_puts(what);
	rt_puts(")\n");
	return CONF_FAIL;
}

long conf_creat(const char *path, const char *data)
{
	long fd = S4(SYS_openat, AT_FDCWD, path, O_CREAT | O_TRUNC | O_RDWR,
		     0644);

	if (fd < 0)
		return fd;
	if (data)
	{
		long n = (long)rt_strlen(data);
		long w = S3(SYS_write, fd, data, n);

		if (w != n)
		{
			(void)S1(SYS_close, fd);
			return w < 0 ? w : -EIO;
		}
	}
	return fd;
}

/*
 * Wait for pid with a bound. Returns the pid on success, -ETIMEDOUT after
 * killing a child that outlived timeout_ms, or the wait4 error.
 */
long conf_child_status(long pid, int *status, long timeout_ms)
{
	long start = rt_now_ms();
	long ret;

	for (;;)
	{
		ret = rt_wait(pid, status, WNOHANG);
		if (ret != 0)
			return ret;
		if (start >= 0 && rt_now_ms() - start > timeout_ms)
			break;
		rt_sleep_ms(2);
	}
	(void)S2(SYS_kill, pid, SIGKILL);
	ret = rt_wait(pid, status, 0);
	return ret < 0 ? ret : -ETIMEDOUT;
}

static void case_dir(char *out, size_t cap, unsigned int idx)
{
	out[0] = '\0';
	rt_cat(out, cap, g_base);
	rt_cat(out, cap, "/c");
	rt_catn(out, cap, (long)idx);
}

static void run_case(const struct conf_entry *c, unsigned int idx)
{
	char dir[160];
	int status = 0;
	long pid;
	long ret;

	case_dir(dir, sizeof(dir), idx);
	(void)S3(SYS_mkdirat, AT_FDCWD, dir, 0755);
	pid = rt_fork();
	if (pid < 0)
	{
		report("", c->name, RESULT_BROK, "fork_errno=", -pid);
		return;
	}
	if (pid == 0)
	{
		if (S1(SYS_chdir, dir) != 0)
			rt_exit(100);
		(void)S1(SYS_umask, 022);
		rt_exit(c->fn());
	}
	ret = conf_child_status(pid, &status, CASE_TIMEOUT_MS);
	if (ret == -ETIMEDOUT)
		report("", c->name, RESULT_BROK, "timeout_ms=", CASE_TIMEOUT_MS);
	else if (ret != pid)
		report("", c->name, RESULT_BROK, "wait_ret=", ret);
	else if (RT_SIGNALED(status))
		report("", c->name, RESULT_BROK, "signal=", RT_TERMSIG(status));
	else if (!RT_EXITED(status) || RT_EXITCODE(status) > CONF_TCONF)
		report("", c->name, RESULT_BROK, "status=", status);
	else
		report("", c->name, RT_EXITCODE(status), "", 0);
	rt_rmtree(AT_FDCWD, dir, 0);
}

/* /ltp/manifest: one LTP testcase binary name per line, under /ltp/bin. */
static void ltp_load(void)
{
	long fd = S4(SYS_openat, AT_FDCWD, "/ltp/manifest", O_RDONLY, 0);
	long n;
	long i;
	unsigned int len = 0;

	if (fd < 0)
		return;
	n = S3(SYS_read, fd, g_ltp_file, sizeof(g_ltp_file) - 1);
	(void)S1(SYS_close, fd);
	if (n <= 0)
		return;
	for (i = 0; i <= n && g_nltp < LTP_MAX; i++)
	{
		char ch = i < n ? g_ltp_file[i] : '\n';

		if (ch == '\n' || ch == '\r')
		{
			if (len)
				g_ltp_names[g_nltp++][len] = '\0';
			len = 0;
		}
		else if (len + 1 < LTP_NAME_MAX)
		{
			g_ltp_names[g_nltp][len++] = ch;
		}
	}
}

static void run_ltp(const char *name, unsigned int idx)
{
	char dir[160];
	char path[96];
	char envtmp[176];
	char *argv[2];
	char *envp[4];
	int status = 0;
	long pid;
	long ret;
	int code;

	case_dir(dir, sizeof(dir), idx);
	(void)S3(SYS_mkdirat, AT_FDCWD, dir, 0755);
	path[0] = '\0';
	rt_cat(path, sizeof(path), "/ltp/bin/");
	rt_cat(path, sizeof(path), name);
	envtmp[0] = '\0';
	rt_cat(envtmp, sizeof(envtmp), "TMPDIR=");
	rt_cat(envtmp, sizeof(envtmp), dir);
	argv[0] = path;
	argv[1] = 0;
	envp[0] = envtmp;
	envp[1] = (char *)"LTPROOT=/ltp";
	envp[2] = (char *)"PATH=/ltp/bin:/bin:/sbin";
	envp[3] = 0;
	pid = rt_fork();
	if (pid == 0)
	{
		(void)S1(SYS_chdir, dir);
		ret = S3(SYS_execve, path, argv, envp);
		rt_exit(ret == -ENOENT ? 127 : 126);
	}
	if (pid < 0)
	{
		report("ltp_", name, RESULT_BROK, "fork_errno=", -pid);
		return;
	}
	ret = conf_child_status(pid, &status, LTP_TIMEOUT_MS);
	if (ret == -ETIMEDOUT)
		report("ltp_", name, RESULT_BROK, "timeout_ms=", LTP_TIMEOUT_MS);
	else if (ret != pid)
		report("ltp_", name, RESULT_BROK, "wait_ret=", ret);
	else if (RT_SIGNALED(status))
		report("ltp_", name, RESULT_BROK, "signal=", RT_TERMSIG(status));
	else
	{
		code = RT_EXITCODE(status);
		if (code == 0)
			report("ltp_", name, CONF_PASS, "", 0);
		else if (code == 126 || code == 127)
			report("ltp_", name, RESULT_BROK, "exec_status=", code);
		else if (code & LTP_TBROK)
			report("ltp_", name, RESULT_BROK, "ltp_exit=", code);
		else if (code & LTP_TFAIL)
			report("ltp_", name, CONF_FAIL, "", 0);
		else if (code & LTP_TCONF)
			report("ltp_", name, CONF_TCONF, "", 0);
		else
			report("ltp_", name, RESULT_BROK, "ltp_exit=", code);
	}
	rt_rmtree(AT_FDCWD, dir, 0);
}

/* Re-exec helper for the exec cases: exit status is the verdict. */
static int probe(int argc, char **argv)
{
	long v = argc > 3 ? rt_atol(argv[3]) : 0;

	if (argc < 3)
		return 99;
	if (rt_streq(argv[2], "exit"))
		return (int)v;
	if (rt_streq(argv[2], "fdclosed"))
		return S2(SYS_fcntl, v, F_GETFD) == -EBADF ? 0 : 1;
	if (rt_streq(argv[2], "fdopen"))
		return S2(SYS_fcntl, v, F_GETFD) >= 0 ? 0 : 1;
	if (rt_streq(argv[2], "argv"))
		return argc == 5 && rt_streq(argv[4], "last") ? 0 : 1;
	if (rt_streq(argv[2], "arglen"))
		return argc == 5 && (long)rt_strlen(argv[4]) == v ? 0 : 1;
	return 98;
}

int rt_main(int argc, char **argv)
{
	unsigned int i;
	long pid = S0(SYS_getpid);

	conf_guest = pid == 1;

	if (argc > 0 && argv[0] && argv[0][0] == '/')
		conf_self = argv[0];
	if (argc > 1 && rt_streq(argv[1], "--probe"))
		return probe(argc, argv);

	/* Host runs (not init) may name a scratch base: conformance <dir>. */
	g_base[0] = '\0';
	rt_cat(g_base, sizeof(g_base),
	       argc > 1 && argv[1][0] == '/' ? argv[1] : "/tmp");
	rt_cat(g_base, sizeof(g_base), "/ir0conf.");
	rt_catn(g_base, sizeof(g_base), pid);
	(void)S3(SYS_mkdirat, AT_FDCWD, g_base, 0755);

	ltp_load();
	rt_puts("KTAP version 1\n");
	rt_puts("# IR0 conformance machine=" RT_MACHINE "\n");
	rt_puts("1..");
	rt_putn((long)(NCASES + g_nltp));
	rt_puts("\n");
	for (i = 0; i < NCASES; i++)
		run_case(&g_cases[i], i);
	for (i = 0; i < g_nltp; i++)
		run_ltp(g_ltp_names[i], (unsigned int)NCASES + i);
	rt_rmtree(AT_FDCWD, g_base, 0);

	rt_puts("# IR0_CONFORMANCE_SUMMARY pass=");
	rt_putn(g_count[CONF_PASS]);
	rt_puts(" fail=");
	rt_putn(g_count[CONF_FAIL]);
	rt_puts(" tconf=");
	rt_putn(g_count[CONF_TCONF]);
	rt_puts(" brok=");
	rt_putn(g_count[RESULT_BROK]);
	rt_puts("\nIR0_CONFORMANCE_DONE\n");
	if (pid != 1)
		return g_count[CONF_FAIL] || g_count[RESULT_BROK] ? 1 : 0;
	/* As init: stay alive so the host autokill sees the done tag. */
	for (;;)
		rt_sleep_ms(1000);
}
