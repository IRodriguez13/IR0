/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: init_pty_winsz_smoke.c
 * Description: TIOCSWINSZ roundtrip + SIGWINCH on PTY slave.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#ifndef TIOCGPTN
#define TIOCGPTN 0x80045430
#endif
#ifndef TIOCSPTLCK
#define TIOCSPTLCK 0x40045431
#endif
#ifndef TIOCSWINSZ
#define TIOCSWINSZ 0x5414
#endif
#ifndef TIOCGWINSZ
#define TIOCGWINSZ 0x5413
#endif

static volatile sig_atomic_t got_winch;

static void on_winch(int sig)
{
	(void)sig;
	got_winch = 1;
}

int main(void)
{
	int master, slave;
	unsigned int ptyn = 0;
	int unlock = 0;
	char path[64];
	struct winsize ws_set = { .ws_row = 40, .ws_col = 120 };
	struct winsize ws_get;
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_winch;
	sigaction(SIGWINCH, &sa, NULL);

	master = open("/dev/ptmx", O_RDWR | O_CLOEXEC);
	if (master < 0)
		return 2;
	if (ioctl(master, TIOCGPTN, &ptyn) != 0)
		return 3;
	if (ioctl(master, TIOCSPTLCK, &unlock) != 0)
		return 4;
	snprintf(path, sizeof(path), "/dev/pts/%u", ptyn);
	slave = open(path, O_RDWR | O_CLOEXEC);
	if (slave < 0)
		return 5;

	if (ioctl(master, TIOCSWINSZ, &ws_set) != 0)
		return 6;
	memset(&ws_get, 0, sizeof(ws_get));
	if (ioctl(slave, TIOCGWINSZ, &ws_get) != 0)
		return 7;
	if (ws_get.ws_row != 40 || ws_get.ws_col != 120)
		return 8;

	(void)write(1, "PTY_WINSZ_OK\n", 13);

	/* Best-effort userspace delivery; kernel emits PTY_WINCH_SENT. */
	{
		int i;
		struct timespec ts = { .tv_sec = 0, .tv_nsec = 5 * 1000 * 1000 };

		for (i = 0; i < 40 && !got_winch; i++)
			(void)nanosleep(&ts, NULL);
	}
	if (got_winch)
		(void)write(1, "PTY_WINCH_OK\n", 13);
	close(slave);
	close(master);
	return 0;
}
