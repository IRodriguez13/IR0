/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: setgroups_efault.c
 * Description: setgroups(2) with a list that runs into an unmapped page
 *              fails with EFAULT and leaves the supplementary groups as
 *              they were: Linux copies the whole list before it commits.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#endif

#include "ir0/ksft.h"

#ifndef IR0_SELFTEST_NOLIBC
#include <errno.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#define GROUPS_PAGE_SIZE 4096UL
#define NR_GROUPS 4
#define MAPPED_GROUPS 2

/* errno(3) EFAULT; nolibc has no <errno.h>. */
#define LINUX_EFAULT 14

static const unsigned int initial[NR_GROUPS] = { 100, 101, 102, 103 };

static int groups_are_initial(void)
{
	unsigned int now[NR_GROUPS * 2];
	long n;
	int i;

	n = (long)syscall(SYS_getgroups, NR_GROUPS * 2, (long)now);
	if (n != NR_GROUPS)
	{
		ksft_print_num("getgroups_ret", n);
		return 0;
	}
	for (i = 0; i < NR_GROUPS; i++)
	{
		if (now[i] != initial[i])
		{
			ksft_print_num("changed_at", i);
			ksft_print_num("group", (long)now[i]);
			return 0;
		}
	}
	return 1;
}

static int setgroups_crossing_unmapped_page_efault(void)
{
	unsigned int *list;
	char *pages;
	long ret;
	int i;

	if (syscall(SYS_setgroups, NR_GROUPS, (long)initial) != 0)
	{
		ksft_print_num("setgroups_errno", errno);
		return 0;
	}
	pages = mmap(0, 2 * GROUPS_PAGE_SIZE, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (pages == MAP_FAILED)
	{
		ksft_print_num("mmap_errno", errno);
		return 0;
	}
	if (munmap(pages + GROUPS_PAGE_SIZE, GROUPS_PAGE_SIZE) != 0)
	{
		ksft_print_num("munmap_errno", errno);
		return 0;
	}
	/* The first entries end the mapped page; the rest are unmapped. */
	list = (unsigned int *)(pages + GROUPS_PAGE_SIZE) - MAPPED_GROUPS;
	for (i = 0; i < MAPPED_GROUPS; i++)
		((volatile unsigned int *)list)[i] = 200 + i;
	errno = 0;
	ret = (long)syscall(SYS_setgroups, NR_GROUPS, (long)list);
	if (ret != -1 || errno != LINUX_EFAULT)
	{
		ksft_print_num("ret", ret);
		ksft_print_num("errno", errno);
		return 0;
	}
	return groups_are_initial();
}

int main(void)
{
	ksft_set_plan(1);
	ksft_test_result(setgroups_crossing_unmapped_page_efault(),
			 "setgroups_crossing_unmapped_page_efault");
	return ksft_exit();
}
