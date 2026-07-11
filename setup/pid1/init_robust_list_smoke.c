/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: init_robust_list_smoke.c
 * Description: set_robust_list / get_robust_list then exit → ROBUST_LIST_EXIT_OK.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdint.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef __NR_set_robust_list
#define __NR_set_robust_list 273
#endif
#ifndef __NR_get_robust_list
#define __NR_get_robust_list 274
#endif

struct robust_list_head
{
	void *list;
	long futex_offset;
	void *list_op_pending;
};

static void tag(const char *s)
{
	const char *p = s;

	while (*p)
		p++;
	(void)write(1, s, (size_t)(p - s));
}

int main(void)
{
	struct robust_list_head head;
	struct robust_list_head *got = 0;
	size_t len = 0;
	long r;

	head.list = 0;
	head.futex_offset = 0;
	head.list_op_pending = 0;

	r = syscall(__NR_set_robust_list, &head, sizeof(head));
	if (r != 0)
		return 2;
	r = syscall(__NR_get_robust_list, 0, &got, &len);
	if (r != 0 || got != &head || len != sizeof(head))
		return 3;
	tag("ROBUST_LIST_OK\n");
	/* exit triggers process_exit_robust_list → ROBUST_LIST_EXIT_OK */
	return 0;
}
