/**
 * IR0 userspace — KTM userdev pilot
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ktm_posix_depth_case.c
 * Description: prlimit64, epoll, pselect6 and PTY winsize depth, via KTM.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/syscall.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "libktm_user.h"

#ifndef SYS_prlimit64
#define SYS_prlimit64 302
#endif
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
#ifndef TIOCSCTTY
#define TIOCSCTTY 0x540E
#endif

static volatile sig_atomic_t got_winch;

static void on_winch(int sig)
{
	(void)sig;
	got_winch = 1;
}

static void say(const char *s)
{
	(void)write(1, s, strlen(s));
}

static int expect(int fd, const char *name, int cond, int *fails)
{
	(void)ktm_assert_true(fd, name, cond);
	if (!cond)
		(*fails)++;
	return cond;
}

static int do_prlimit(int pid, int resource, const struct rlimit *n,
		      struct rlimit *o)
{
	return (int)syscall(SYS_prlimit64, pid, resource, n, o);
}

int main(void)
{
	int fd;
	int fails = 0;
	struct rlimit rl;
	struct rlimit check;
	int pfd[2];
	int epfd;
	struct epoll_event ev;
	struct epoll_event out;
	fd_set rfds;
	struct timespec ts;
	int n;
	int master;
	int slave;
	unsigned int ptyn = 0;
	int unlock = 0;
	char path[64];
	struct winsize ws_set = { .ws_row = 40, .ws_col = 120 };
	struct winsize ws_get;
	char buf[8];

	fd = ktm_open();
	if (fd < 0)
	{
		say("KTM_USERDEV_FAIL open\n");
		return 1;
	}

	if (ktm_case_begin(fd, "posix_depth") != 0)
	{
		say("KTM_USERDEV_FAIL case_begin\n");
		ktm_close(fd);
		return 1;
	}

	/* prlimit64 roundtrip. */
	if (getrlimit(RLIMIT_NOFILE, &rl) != 0)
	{
		expect(fd, "getrlimit", 0, &fails);
		goto done;
	}
	expect(fd, "getrlimit", 1, &fails);
	expect(fd, "nofile_soft",
	       rl.rlim_cur != 0 && rl.rlim_cur != RLIM_INFINITY, &fails);
	expect(fd, "prlimit64_get",
	       do_prlimit(0, RLIMIT_NOFILE, NULL, &check) == 0 &&
		       check.rlim_cur == rl.rlim_cur,
	       &fails);
	if (rl.rlim_cur > 16)
	{
		struct rlimit lower = rl;

		lower.rlim_cur = 16;
		expect(fd, "prlimit64_set",
		       do_prlimit(0, RLIMIT_NOFILE, &lower, NULL) == 0 &&
			       do_prlimit(0, RLIMIT_NOFILE, NULL, &check) == 0 &&
			       check.rlim_cur == 16,
		       &fails);
		(void)do_prlimit(0, RLIMIT_NOFILE, &rl, NULL);
	}

	/* epoll: create, add a pipe reader, wake it. */
	if (pipe(pfd) != 0)
	{
		expect(fd, "pipe", 0, &fails);
		goto done;
	}
	expect(fd, "pipe", 1, &fails);
	epfd = epoll_create1(0);
	if (epfd < 0)
	{
		expect(fd, "epoll_create1", 0, &fails);
		goto done;
	}
	expect(fd, "epoll_create1", 1, &fails);
	memset(&ev, 0, sizeof(ev));
	ev.events = EPOLLIN;
	ev.data.fd = pfd[0];
	expect(fd, "epoll_ctl",
	       epoll_ctl(epfd, EPOLL_CTL_ADD, pfd[0], &ev) == 0, &fails);
	expect(fd, "pipe_write", write(pfd[1], "x", 1) == 1, &fails);
	n = epoll_wait(epfd, &out, 1, 1000);
	expect(fd, "epoll_wait",
	       n == 1 && (out.events & EPOLLIN) != 0, &fails);

	/* pselect6 sees the same pipe as readable. */
	FD_ZERO(&rfds);
	FD_SET(pfd[0], &rfds);
	ts.tv_sec = 0;
	ts.tv_nsec = 100000000L;
	n = pselect(pfd[0] + 1, &rfds, NULL, NULL, &ts, NULL);
	expect(fd, "pselect6",
	       n >= 1 && FD_ISSET(pfd[0], &rfds) != 0, &fails);

	/* PTY winsize roundtrip. */
	master = open("/dev/ptmx", O_RDWR | O_CLOEXEC);
	if (master < 0)
	{
		expect(fd, "ptmx_open", 0, &fails);
		goto done;
	}
	expect(fd, "ptmx_open", 1, &fails);
	expect(fd, "tiocgptn", ioctl(master, TIOCGPTN, &ptyn) == 0, &fails);
	expect(fd, "tiocsptlck", ioctl(master, TIOCSPTLCK, &unlock) == 0, &fails);
	snprintf(path, sizeof(path), "/dev/pts/%u", ptyn);
	slave = open(path, O_RDWR | O_CLOEXEC);
	if (slave < 0)
	{
		expect(fd, "pts_open", 0, &fails);
		close(master);
		goto done;
	}
	expect(fd, "pts_open", 1, &fails);

	/* TIOCSCTTY requires a session leader; PID1 already is one. */
	if (getsid(0) != getpid())
		expect(fd, "setsid", setsid() >= 0, &fails);
	expect(fd, "tiocsctty", ioctl(master, TIOCSCTTY, 0) == 0, &fails);

	{
		struct sigaction sa;

		memset(&sa, 0, sizeof(sa));
		sa.sa_handler = on_winch;
		expect(fd, "sigwinch_handler",
		       sigaction(SIGWINCH, &sa, NULL) == 0, &fails);
	}

	expect(fd, "tiocswinsz",
	       ioctl(master, TIOCSWINSZ, &ws_set) == 0, &fails);
	memset(&ws_get, 0, sizeof(ws_get));
	expect(fd, "tiocgwinsz",
	       ioctl(slave, TIOCGWINSZ, &ws_get) == 0 &&
		       ws_get.ws_row == ws_set.ws_row &&
		       ws_get.ws_col == ws_set.ws_col,
	       &fails);

	{
		struct timespec ts = { .tv_sec = 0, .tv_nsec = 5 * 1000 * 1000 };
		int i;

		for (i = 0; i < 40 && !got_winch; i++)
			(void)nanosleep(&ts, NULL);
	}
	expect(fd, "sigwinch", got_winch != 0, &fails);

	expect(fd, "pty_mwrite", write(master, "P", 1) == 1, &fails);
	memset(buf, 0, sizeof(buf));
	expect(fd, "pty_sread",
	       read(slave, buf, 1) == 1 && buf[0] == 'P', &fails);

	close(slave);
	close(master);
	close(epfd);
	close(pfd[0]);
	close(pfd[1]);

done:
	(void)ktm_case_end(fd, "posix_depth", fails == 0 ? 0 : 1);
	ktm_close(fd);

	if (fails == 0)
	{
		say("KTM_USERDEV_OK\n");
		return 0;
	}
	say("KTM_USERDEV_FAIL\n");
	return 1;
}
