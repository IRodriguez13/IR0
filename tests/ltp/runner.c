/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: runner.c
 * Description: PID 1 that runs a list of LTP tests with a per-test timeout.
 *
 * Reads /ltp/runlist: "@timeout SEC" sets the per-test limit, every other
 * non-comment line is "TAG PATH ARGV0 [ARGS...]". Each test runs in its own
 * process group. One line per event goes to stdout:
 *
 *   LTP_BEGIN <tag>
 *   LTP_RESULT <tag> <PASS|FAIL|BROK|TWARN|TCONF|HANG> exit=N sig=N ms=N
 *   LTP_RUN_DONE total=N
 *
 * scripts/ltp_harness.py turns those lines into ltp_results.tsv.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define RUNLIST "/ltp/runlist"
#define MAX_LINE 512
#define MAX_ARGS 16
#define KILL_GRACE_MS 5000
#define POLL_MS 20

/* LTP exit status bits (include/tst_res_flags.h). */
#define LTP_TFAIL 1
#define LTP_TBROK 2
#define LTP_TWARN 4
#define LTP_TCONF 32

static char *const test_env[] = {
	"PATH=/ltp/bin",
	"TMPDIR=/tmp",
	"LTPROOT=/ltp",
	"HOME=/tmp",
	"LTP_COLORIZE_OUTPUT=0",
	NULL,
};

/* A leading newline separates our line from unterminated test output. */
static void emit(const char *fmt, ...)
{
	char line[MAX_LINE + 64];
	va_list ap;
	int len;

	line[0] = '\n';
	va_start(ap, fmt);
	len = vsnprintf(line + 1, sizeof(line) - 2, fmt, ap);
	va_end(ap);
	if (len < 0)
		return;
	if ((size_t)len > sizeof(line) - 3)
		len = (int)(sizeof(line) - 3);
	line[1 + len] = '\n';
	(void)write(STDOUT_FILENO, line, (size_t)len + 2);
}

static long now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void sleep_ms(long ms)
{
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };

	while (nanosleep(&ts, &ts) != 0 && errno == EINTR)
		;
}

static const char *classify(int status)
{
	int code;

	if (WIFSIGNALED(status))
		return "BROK";
	code = WEXITSTATUS(status);
	if (code == 0)
		return "PASS";
	if (code & ~(LTP_TFAIL | LTP_TBROK | LTP_TWARN | LTP_TCONF))
		return "BROK";
	if (code & LTP_TBROK)
		return "BROK";
	if (code & LTP_TFAIL)
		return "FAIL";
	if (code & LTP_TWARN)
		return "TWARN";
	return "TCONF";
}

/*
 * PID 1 also inherits every orphan, so reap any child and report whether
 * @pid itself was collected.
 */
static int reap(pid_t pid, int *status)
{
	pid_t got;
	int st;

	while ((got = waitpid(-1, &st, WNOHANG)) > 0)
	{
		if (got == pid)
		{
			*status = st;
			return 1;
		}
	}
	return 0;
}

static void run_one(const char *tag, const char *path, char **argv,
		    long timeout_ms)
{
	const char *verdict;
	long start;
	int status = 0;
	int done;
	pid_t pid;

	emit("LTP_BEGIN %s", tag);
	start = now_ms();
	pid = fork();
	if (pid < 0)
	{
		emit("LTP_RESULT %s BROK exit=-1 sig=0 ms=0", tag);
		return;
	}
	if (pid == 0)
	{
		setpgid(0, 0);
		execve(path, argv, test_env);
		_exit(127);
	}
	setpgid(pid, pid);

	done = 0;
	while (!(done = reap(pid, &status)) && now_ms() - start < timeout_ms)
		sleep_ms(POLL_MS);
	if (!done)
	{
		long kill_at = now_ms();

		kill(-pid, SIGKILL);
		kill(pid, SIGKILL);
		while (!(done = reap(pid, &status)) &&
		       now_ms() - kill_at < KILL_GRACE_MS)
			sleep_ms(POLL_MS);
		/* Killed on timeout: LTP itself reports this as TBROK. */
		verdict = done ? "BROK" : "HANG";
		emit("LTP_RESULT %s %s exit=-1 sig=%d ms=%ld timeout=1", tag,
		     verdict, SIGKILL, now_ms() - start);
		return;
	}
	verdict = classify(status);
	emit("LTP_RESULT %s %s exit=%d sig=%d ms=%ld", tag, verdict,
	     WIFEXITED(status) ? WEXITSTATUS(status) : -1,
	     WIFSIGNALED(status) ? WTERMSIG(status) : 0, now_ms() - start);
}

static int split(char *line, char **argv)
{
	int argc = 0;
	char *tok;

	for (tok = strtok(line, " \t\r\n"); tok && argc < MAX_ARGS;
	     tok = strtok(NULL, " \t\r\n"))
		argv[argc++] = tok;
	argv[argc] = NULL;
	return argc;
}

int main(void)
{
	char line[MAX_LINE];
	char *argv[MAX_ARGS + 1];
	long timeout_ms = 120000;
	int total = 0;
	FILE *list;

	if (fcntl(STDOUT_FILENO, F_GETFD) < 0)
	{
		int fd = open("/dev/console", O_RDWR);

		if (fd >= 0)
		{
			dup2(fd, STDIN_FILENO);
			dup2(fd, STDOUT_FILENO);
			dup2(fd, STDERR_FILENO);
		}
	}
	mkdir("/tmp", 01777);
	chmod("/tmp", 01777);
	if (chdir("/tmp") != 0)
		emit("LTP_WARN chdir /tmp errno=%d", errno);

	list = fopen(RUNLIST, "r");
	if (!list)
	{
		emit("LTP_RUN_ERROR open %s errno=%d", RUNLIST, errno);
		emit("LTP_RUN_DONE total=0");
		for (;;)
			sleep(3600);
	}
	while (fgets(line, sizeof(line), list))
	{
		int argc;
		char *path;

		argc = split(line, argv);
		if (argc == 0 || argv[0][0] == '#')
			continue;
		if (strcmp(argv[0], "@timeout") == 0)
		{
			if (argc > 1 && atol(argv[1]) > 0)
				timeout_ms = atol(argv[1]) * 1000L;
			continue;
		}
		if (argc < 3)
			continue;
		path = argv[1];
		run_one(argv[0], path, &argv[2], timeout_ms);
		total++;
	}
	fclose(list);
	sync();
	emit("LTP_RUN_DONE total=%d", total);
	for (;;)
		sleep(3600);
}
