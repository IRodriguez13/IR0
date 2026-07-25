/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: binprobe_smoke.c
 * Description: Probe product ELFs + BusyBox applets; print BINPROBE lines.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define TIMEOUT_SEC 3

static void put(const char *s)
{
	if (s)
		(void)write(1, s, strlen(s));
}

static void put_int(int v)
{
	char buf[16];
	int i = (int)sizeof(buf);
	int neg = v < 0;
	unsigned int u = neg ? (unsigned int)(-v) : (unsigned int)v;

	buf[--i] = '\0';
	do
	{
		buf[--i] = (char)('0' + (u % 10u));
		u /= 10u;
	} while (u && i > 1);
	if (neg && i > 0)
		buf[--i] = '-';
	put(&buf[i]);
}

static volatile sig_atomic_t timed_out;

static void on_alrm(int sig)
{
	(void)sig;
	timed_out = 1;
}

/*
 * Run argv; classify: ok (0), fail (nonzero), timeout, execfail, panic-avoid.
 * Skip poweroff/halt/reboot — those stop the machine.
 */
static void probe_exec(const char *tag, char *const argv[])
{
	pid_t pid;
	int status;
	const char *result = "ok";
	int ec = 0;

	if (!argv || !argv[0])
		return;

	timed_out = 0;
	signal(SIGALRM, on_alrm);
	alarm(TIMEOUT_SEC);
	pid = fork();
	if (pid < 0)
	{
		put("BINPROBE tag=");
		put(tag);
		put(" status=forkfail ec=");
		put_int(-errno);
		put("\n");
		alarm(0);
		return;
	}
	if (pid == 0)
	{
		int nullfd = open("/dev/null", O_RDWR);

		if (nullfd >= 0)
		{
			dup2(nullfd, 0);
			dup2(nullfd, 1);
			dup2(nullfd, 2);
			if (nullfd > 2)
				close(nullfd);
		}
		execv(argv[0], argv);
		_exit(127);
	}

	if (waitpid(pid, &status, 0) < 0)
	{
		result = "waitfail";
		ec = -errno;
	}
	else if (timed_out)
	{
		kill(pid, SIGKILL);
		(void)waitpid(pid, &status, 0);
		result = "timeout";
		ec = -1;
	}
	else if (WIFEXITED(status))
	{
		ec = WEXITSTATUS(status);
		if (ec == 127)
			result = "execfail";
		else if (ec != 0)
			result = "fail";
		else
			result = "ok";
	}
	else if (WIFSIGNALED(status))
	{
		result = "signal";
		ec = WTERMSIG(status);
	}
	alarm(0);

	put("BINPROBE tag=");
	put(tag);
	put(" status=");
	put(result);
	put(" ec=");
	put_int(ec);
	put("\n");
}

static int is_dangerous(const char *name)
{
	return strcmp(name, "halt") == 0 || strcmp(name, "poweroff") == 0 ||
	       strcmp(name, "reboot") == 0 || strcmp(name, "init") == 0 ||
	       strcmp(name, "runit-init") == 0 || strcmp(name, "runit") == 0 ||
	       strcmp(name, "linuxrc") == 0;
}

static void probe_path_help(const char *path)
{
	char tag[160];
	char *argv[3];
	const char *base;

	base = strrchr(path, '/');
	base = base ? base + 1 : path;
	if (is_dangerous(base))
	{
		put("BINPROBE tag=");
		put(path);
		put(" status=skip ec=0\n");
		return;
	}

	snprintf(tag, sizeof(tag), "%s", path);
	argv[0] = (char *)path;
	argv[1] = "--help";
	argv[2] = NULL;
	probe_exec(tag, argv);
}

static void probe_dir(const char *dir)
{
	DIR *d;
	struct dirent *ent;
	char path[256];
	struct stat st;

	d = opendir(dir);
	if (!d)
	{
		put("BINPROBE tag=");
		put(dir);
		put(" status=nodir ec=");
		put_int(-errno);
		put("\n");
		return;
	}
	while ((ent = readdir(d)) != NULL)
	{
		if (ent->d_name[0] == '.')
			continue;
		snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
		if (stat(path, &st) != 0)
			continue;
		if (!S_ISREG(st.st_mode) || !(st.st_mode & 0111))
			continue;
		probe_path_help(path);
	}
	closedir(d);
}

static void probe_busybox_list(void)
{
	int fd;
	char buf[8192];
	ssize_t n;
	char *p;
	char *line;

	/*
	 * Prefer a host-injected list: pipe+busybox --list is flaky on this
	 * rootfs (short reads). Fallback to live --list into a temp file.
	 */
	fd = open("/etc/busybox.list", O_RDONLY);
	if (fd < 0)
	{
		pid_t pid;
		int outfd;

		outfd = open("/tmp/bb.list", O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (outfd < 0)
			return;
		pid = fork();
		if (pid == 0)
		{
			char *argv[] = { "/bin/busybox", "--list", NULL };

			dup2(outfd, 1);
			close(outfd);
			execv(argv[0], argv);
			_exit(127);
		}
		close(outfd);
		(void)waitpid(pid, NULL, 0);
		fd = open("/tmp/bb.list", O_RDONLY);
		if (fd < 0)
			return;
	}
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return;
	buf[n] = '\0';
	p = buf;
	while ((line = strsep(&p, "\n")) != NULL)
	{
		char *argv[4];
		char tag[80];

		if (!line[0] || is_dangerous(line))
		{
			if (line[0])
			{
				put("BINPROBE tag=busybox:");
				put(line);
				put(" status=skip ec=0\n");
			}
			continue;
		}
		snprintf(tag, sizeof(tag), "busybox:%s", line);
		argv[0] = "/bin/busybox";
		argv[1] = line;
		argv[2] = "--help";
		argv[3] = NULL;
		probe_exec(tag, argv);
	}
}

int main(void)
{
	put("BINPROBE_BEGIN\n");
	probe_dir("/bin");
	probe_dir("/sbin");
	probe_dir("/usr/bin");
	probe_dir("/usr/sbin");
	probe_busybox_list();
	put("BINPROBE_OK\n");
	/* Prefer clean QEMU exit when isa-debug-exit is present. */
	(void)reboot(RB_HALT_SYSTEM);
	for (;;)
		sleep(3600);
	return 0;
}
