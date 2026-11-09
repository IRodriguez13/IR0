/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: uaccess_efault_keeps_data.c
 * Description: A read whose destination cannot take the bytes fails with
 *              EFAULT and consumes nothing (Linux copies before it dequeues):
 *              the next read with a good buffer still gets the data, for a
 *              UNIX stream socket through read(2) and recv(2), and a pipe.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#endif

#include "ir0/ksft.h"

#ifndef IR0_SELFTEST_NOLIBC
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#define RO_PAGE_SIZE 4096UL
#define MSG_BYTES 4

/* errno(3) EFAULT; nolibc has no <errno.h>. */
#define LINUX_EFAULT 14

static const char msg[MSG_BYTES] = { 'a', 'b', 'c', 'd' };

/* A mapped, user-readable page the kernel must not write into. */
static char *readonly_page(void)
{
	char *p;

	p = mmap(0, RO_PAGE_SIZE, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED)
	{
		ksft_print_num("mmap_errno", errno);
		return 0;
	}
	*(volatile char *)p = 1;
	if (mprotect(p, RO_PAGE_SIZE, PROT_READ) != 0)
	{
		ksft_print_num("mprotect_errno", errno);
		return 0;
	}
	return p;
}

static int is_efault(long ret)
{
	if (ret == -1 && errno == LINUX_EFAULT)
		return 1;
	ksft_print_num("bad_read_ret", ret);
	ksft_print_num("bad_read_errno", errno);
	return 0;
}

static int got_msg(long ret, const char *buf)
{
	unsigned long i;

	if (ret != MSG_BYTES)
	{
		ksft_print_num("good_read_ret", ret);
		ksft_print_num("good_read_errno", errno);
		return 0;
	}
	for (i = 0; i < MSG_BYTES; i++)
	{
		if (buf[i] != msg[i])
		{
			ksft_print_num("mismatch_at", (long)i);
			return 0;
		}
	}
	return 1;
}

/* use_recv: recv(2) instead of read(2) on the receiving end. */
static int socket_keeps_data(char *ro, int use_recv)
{
	char buf[MSG_BYTES];
	int sv[2];
	long ret;
	int ok;

	if (!ro)
		return 0;
	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sv) != 0)
	{
		ksft_print_num("socketpair_errno", errno);
		return 0;
	}
	if (write(sv[0], msg, MSG_BYTES) != MSG_BYTES)
	{
		ksft_print_num("write_errno", errno);
		return 0;
	}
	errno = 0;
	ret = use_recv ? (long)recv(sv[1], ro, MSG_BYTES, 0)
		       : (long)read(sv[1], ro, MSG_BYTES);
	ok = is_efault(ret);
	errno = 0;
	ret = use_recv ? (long)recv(sv[1], buf, MSG_BYTES, 0)
		       : (long)read(sv[1], buf, MSG_BYTES);
	ok = got_msg(ret, buf) && ok;
	close(sv[0]);
	close(sv[1]);
	return ok;
}

static int pipe_keeps_data(char *ro)
{
	char buf[MSG_BYTES];
	int fds[2];
	long ret;
	int ok;

	if (!ro)
		return 0;
	if (pipe2(fds, O_NONBLOCK) != 0)
	{
		ksft_print_num("pipe2_errno", errno);
		return 0;
	}
	if (write(fds[1], msg, MSG_BYTES) != MSG_BYTES)
	{
		ksft_print_num("write_errno", errno);
		return 0;
	}
	errno = 0;
	ok = is_efault((long)read(fds[0], ro, MSG_BYTES));
	errno = 0;
	ret = (long)read(fds[0], buf, MSG_BYTES);
	ok = got_msg(ret, buf) && ok;
	close(fds[0]);
	close(fds[1]);
	return ok;
}

int main(void)
{
	char *ro = readonly_page();

	ksft_set_plan(3);
	ksft_test_result(socket_keeps_data(ro, 0),
			 "socket_read_efault_keeps_data");
	ksft_test_result(socket_keeps_data(ro, 1),
			 "socket_recv_efault_keeps_data");
	ksft_test_result(pipe_keeps_data(ro), "pipe_read_efault_keeps_data");
	return ksft_exit();
}
