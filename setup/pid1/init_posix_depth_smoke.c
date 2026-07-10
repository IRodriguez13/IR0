/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: init_posix_depth_smoke.c
 * Description: Userspace smoke — prlimit, epoll, pselect6, PTY/TIOCGWINSZ
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/syscall.h>
#include <termios.h>
#include <unistd.h>

#ifndef SYS_prlimit64
#define SYS_prlimit64 302
#endif

static int do_prlimit(int pid, int resource, const struct rlimit *n,
		      struct rlimit *o)
{
	return (int)syscall(SYS_prlimit64, pid, resource, n, o);
}

static void write_str(const char *s)
{
	const char *p = s;

	while (*p)
		p++;
	(void)write(1, s, (size_t)(p - s));
}

static void fail(const char *tag)
{
	write_str("[POSIX][FAIL] ");
	write_str(tag);
	write_str("\n");
}

int main(void)
{
	struct rlimit rl;
	int epfd;
	int pfd[2];
	struct epoll_event ev;
	struct epoll_event out;
	int n;
	fd_set rfds;
	struct timespec ts;
	int master;
	int slave;
	unsigned int ptyn = 99;
	struct winsize ws;
	char buf[8];
	int zero = 0;

	if (getrlimit(RLIMIT_NOFILE, &rl) != 0)
	{
		fail("getrlimit");
		return 2;
	}
	if (rl.rlim_cur == 0 || rl.rlim_cur == RLIM_INFINITY)
	{
		fail("nofile_soft");
		return 3;
	}
	if (do_prlimit(0, RLIMIT_NOFILE, NULL, &rl) != 0)
	{
		fail("prlimit");
		return 4;
	}

	if (pipe(pfd) != 0)
	{
		fail("pipe");
		return 5;
	}
	epfd = epoll_create1(0);
	if (epfd < 0)
	{
		fail("epoll_create1");
		return 6;
	}
	memset(&ev, 0, sizeof(ev));
	ev.events = EPOLLIN;
	ev.data.fd = pfd[0];
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, pfd[0], &ev) != 0)
	{
		fail("epoll_ctl");
		return 7;
	}
	if (write(pfd[1], "x", 1) != 1)
	{
		fail("pipe_write");
		return 8;
	}
	n = epoll_wait(epfd, &out, 1, 1000);
	if (n != 1 || !(out.events & EPOLLIN))
	{
		fail("epoll_wait");
		return 9;
	}

	FD_ZERO(&rfds);
	FD_SET(pfd[0], &rfds);
	ts.tv_sec = 0;
	ts.tv_nsec = 100000000L;
	n = pselect(pfd[0] + 1, &rfds, NULL, NULL, &ts, NULL);
	if (n < 1 || !FD_ISSET(pfd[0], &rfds))
	{
		fail("pselect6");
		return 10;
	}

	if (ioctl(1, TIOCGWINSZ, &ws) != 0 || ws.ws_col == 0 || ws.ws_row == 0)
	{
		fail("tiocgwinsz_stdio");
		return 11;
	}

	master = open("/dev/ptmx", O_RDWR);
	if (master < 0)
	{
		fail("ptmx_open");
		return 12;
	}
	if (ioctl(master, TIOCGPTN, &ptyn) != 0 || ptyn != 0)
	{
		fail("tiocgptn");
		return 13;
	}
	if (ioctl(master, TIOCSPTLCK, &zero) != 0)
	{
		fail("tiocsptlck");
		return 14;
	}
	slave = open("/dev/pts/0", O_RDWR);
	if (slave < 0)
	{
		fail("pts_open");
		return 15;
	}
	memset(&ws, 0, sizeof(ws));
	if (ioctl(slave, TIOCGWINSZ, &ws) != 0 || ws.ws_col == 0)
	{
		fail("tiocgwinsz_pty");
		return 16;
	}
	if (write(master, "P", 1) != 1)
	{
		fail("pty_mwrite");
		return 17;
	}
	memset(buf, 0, sizeof(buf));
	if (read(slave, buf, 1) != 1 || buf[0] != 'P')
	{
		fail("pty_sread");
		return 18;
	}

	close(slave);
	close(master);
	close(epfd);
	close(pfd[0]);
	close(pfd[1]);

	write_str("[POSIX][CLASSIFY] POSIX_DEPTH_OK\n");
	write_str("POSIX_DEPTH_OK\n");
	return 0;
}
