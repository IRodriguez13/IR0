/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: cow_mprotect.c
 * Description: mprotect(PROT_READ) on a page a fork left copy-on-write
 *              makes it read-only for real: a store raises SIGSEGV and
 *              read(2) into it fails with EFAULT, as on Linux, instead of
 *              copying the page and writing the private copy. Granting
 *              PROT_WRITE again gives a private writable page.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#endif

#include "ir0/ksft.h"

#ifndef IR0_SELFTEST_NOLIBC
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifndef OPEN_EXISTING_PATH
#define OPEN_EXISTING_PATH "/proc/uptime"
#endif

#define COW_PAGE_SIZE 4096UL
#define PROBE_BYTES 16
#define FILL_BYTE 0x5a
#define CHILD_WROTE 3

/* errno(3) EFAULT; nolibc has no <errno.h>. */
#define LINUX_EFAULT 14

static int page_holds(const char *page, char byte)
{
	const volatile char *p = page;
	unsigned long i;

	for (i = 0; i < COW_PAGE_SIZE; i++)
	{
		if (p[i] != byte)
		{
			ksft_print_num("changed_at", (long)i);
			return 0;
		}
	}
	return 1;
}

/* A page the parent writes before fork, so fork leaves it copy-on-write. */
static char *cow_page(void)
{
	volatile char *fill;
	char *p;
	unsigned long i;

	p = mmap(0, COW_PAGE_SIZE, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED)
	{
		ksft_print_num("mmap_errno", errno);
		return 0;
	}
	/* volatile: keep the fill out of a libc memset (nolibc has none). */
	fill = p;
	for (i = 0; i < COW_PAGE_SIZE; i++)
		fill[i] = FILL_BYTE;
	return p;
}

static int wait_child(int pid, int *status)
{
	if (pid < 0)
	{
		ksft_print_num("fork_errno", errno);
		return 0;
	}
	if (wait4(pid, status, 0, 0) != pid)
	{
		ksft_print_num("wait4_errno", errno);
		return 0;
	}
	return 1;
}

static void child_store_after_prot_read(char *p)
{
	volatile char *v = p;

	if (mprotect(p, COW_PAGE_SIZE, PROT_READ) != 0)
		_exit(1);
	v[0] = 1;
	_exit(CHILD_WROTE);
}

static int store_after_prot_read_segv(void)
{
	char *p = cow_page();
	int status = 0;
	int pid;

	if (!p)
		return 0;
	pid = fork();
	if (pid == 0)
		child_store_after_prot_read(p);
	if (!wait_child(pid, &status))
		return 0;
	if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGSEGV)
	{
		ksft_print_num("child_status", status);
		return 0;
	}
	return page_holds(p, FILL_BYTE);
}

static void child_read_after_prot_read(char *p)
{
	long ret;
	int fd;

	if (mprotect(p, COW_PAGE_SIZE, PROT_READ) != 0)
		_exit(1);
	fd = open(OPEN_EXISTING_PATH, O_RDONLY);
	if (fd < 0)
		_exit(2);
	errno = 0;
	ret = (long)read(fd, p, PROBE_BYTES);
	close(fd);
	if (ret != -1 || errno != LINUX_EFAULT)
	{
		ksft_print_num("ret", ret);
		ksft_print_num("errno", errno);
		_exit(CHILD_WROTE);
	}
	_exit(page_holds(p, FILL_BYTE) ? 0 : 4);
}

static int read_after_prot_read_efault(void)
{
	char *p = cow_page();
	int status = 0;
	int pid;

	if (!p)
		return 0;
	pid = fork();
	if (pid == 0)
		child_read_after_prot_read(p);
	if (!wait_child(pid, &status))
		return 0;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
	{
		ksft_print_num("child_status", status);
		return 0;
	}
	return page_holds(p, FILL_BYTE);
}

static void child_store_after_prot_write(char *p)
{
	volatile char *v = p;

	if (mprotect(p, COW_PAGE_SIZE, PROT_READ) != 0 ||
	    mprotect(p, COW_PAGE_SIZE, PROT_READ | PROT_WRITE) != 0)
		_exit(1);
	v[0] = 1;
	_exit(v[0] == 1 && v[1] == FILL_BYTE ? 0 : 2);
}

/* The child's store lands in its own copy, never in the parent's page. */
static int store_after_prot_write_private(void)
{
	char *p = cow_page();
	int status = 0;
	int pid;

	if (!p)
		return 0;
	pid = fork();
	if (pid == 0)
		child_store_after_prot_write(p);
	if (!wait_child(pid, &status))
		return 0;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
	{
		ksft_print_num("child_status", status);
		return 0;
	}
	return page_holds(p, FILL_BYTE);
}

int main(void)
{
	ksft_set_plan(3);
	ksft_test_result(store_after_prot_read_segv(),
			 "cow_store_after_prot_read_segv");
	ksft_test_result(read_after_prot_read_efault(),
			 "cow_read_after_prot_read_efault");
	ksft_test_result(store_after_prot_write_private(),
			 "cow_store_after_prot_write_private");
	return ksft_exit();
}
