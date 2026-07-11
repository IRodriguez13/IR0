/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: init_posix_sighup_tty_smoke.c
 * Description: PTY TIOCSCTTY + master close → SIGHUP to foreground pgrp.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef TIOCSCTTY
#define TIOCSCTTY 0x540E
#endif
#ifndef TIOCSPTLCK
#define TIOCSPTLCK 0x40045431u
#endif

static void tag(const char *s)
{
	const char *p = s;

	while (*p)
		p++;
	(void)write(1, s, (size_t)(p - s));
}

int main(void)
{
	int master = -1;
	int slave = -1;
	int unlock = 0;
	pid_t child;
	int status = 0;

	master = open("/dev/ptmx", O_RDWR | O_CLOEXEC);
	if (master < 0)
	{
		tag("SIGHUP_OPEN_PTMX_FAIL\n");
		return 1;
	}
	if (ioctl(master, TIOCSPTLCK, &unlock) != 0)
	{
		tag("SIGHUP_UNLOCK_FAIL\n");
		return 1;
	}
	slave = open("/dev/pts/0", O_RDWR | O_CLOEXEC);
	if (slave < 0)
	{
		tag("SIGHUP_OPEN_PTS_FAIL\n");
		return 1;
	}

	child = fork();
	if (child < 0)
	{
		tag("SIGHUP_FORK_FAIL\n");
		return 1;
	}

	if (child == 0)
	{
		close(master);
		if (setsid() < 0)
			_exit(3);
		if (ioctl(slave, TIOCSCTTY, 0) != 0)
			_exit(4);
		tag("SIGHUP_CHILD_ARMED\n");
		/* Default SIGHUP terminates; parent expects WIFSIGNALED. */
		for (;;)
			(void)pause();
	}

	close(slave);
	(void)usleep(100000);
	close(master);
	tag("SIGHUP_MASTER_CLOSED\n");

	if (waitpid(child, &status, 0) < 0)
	{
		tag("SIGHUP_WAIT_FAIL\n");
		return 1;
	}
	if (WIFSIGNALED(status) && WTERMSIG(status) == SIGHUP)
	{
		tag("SIGHUP_OK\n");
		tag("POSIX_SIGHUP_TTY_OK\n");
		return 0;
	}
	/* Also accept clean exit if a handler was installed by libc. */
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
	{
		tag("SIGHUP_OK\n");
		tag("POSIX_SIGHUP_TTY_OK\n");
		return 0;
	}
	tag("SIGHUP_WAIT_FAIL\n");
	return 1;
}
