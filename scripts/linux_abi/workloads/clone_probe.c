/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: clone_probe.c
 * Description: clone(CLONE_VM|CLONE_VFORK) child stack ABI audit
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

static int child_mark_rsp(void *arg)
{
	uintptr_t *slot = arg;
	uintptr_t rsp;

	__asm__ volatile("mov %%rsp, %0" : "=r"(rsp));
	*slot = rsp;
	return 0;
}

#ifndef MAP_STACK
#define MAP_STACK 0
#endif

static void audit_cl(unsigned step, const char *op, long ret, int err)
{
	char buf[192];
	int n;

	n = snprintf(buf, sizeof(buf),
		     "[LINUX_ABI_AUDIT][clone] step=%u op=%s ret=%ld errno=%d\n",
		     step, op, ret, err);
	if (n > 0)
		(void)write(1, buf, (size_t)n);
}

int main(void)
{
	enum
	{
		STACK_BYTES = 8192
	};
	void *stack;
	char *sp;
	long pid;
	int st;
	uintptr_t *sp_slot;
	long bad;

	stack = mmap(NULL, STACK_BYTES, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
	audit_cl(0, "mmap_stack", stack == MAP_FAILED ? -1L : 0L,
		 stack == MAP_FAILED ? errno : 0);
	if (stack == MAP_FAILED)
		return 1;

	sp_slot = (uintptr_t *)stack;
	*sp_slot = 0;
	/* Exclusive mapping top minus 16 so the first push stays mapped. */
	sp = (char *)(((uintptr_t)stack + STACK_BYTES - 16) & ~(uintptr_t)15);

	/* musl __clone: fn on the child stack, not a raw SYS_clone wrapper. */
	pid = clone(child_mark_rsp, sp, CLONE_VM | CLONE_VFORK | SIGCHLD, sp_slot);
	audit_cl(1, "clone_vfork_stack", pid, pid < 0 ? errno : 0);
	if (pid < 0)
	{
		(void)munmap(stack, STACK_BYTES);
		return 1;
	}

	if (waitpid((pid_t)pid, &st, 0) != (pid_t)pid || !WIFEXITED(st) ||
	    WEXITSTATUS(st) != 0)
	{
		audit_cl(2, "wait_child", -1L, errno);
		(void)munmap(stack, STACK_BYTES);
		return 1;
	}
	audit_cl(2, "wait_child", 0L, 0);

	if (*sp_slot < (uintptr_t)stack ||
	    *sp_slot > (uintptr_t)stack + STACK_BYTES)
	{
		audit_cl(3, "child_rsp_in_stack", -1L, 0);
		(void)munmap(stack, STACK_BYTES);
		return 1;
	}
	audit_cl(3, "child_rsp_in_stack", 0L, 0);

	bad = syscall(SYS_clone, (unsigned long)CLONE_THREAD, NULL, NULL, NULL,
		      0UL);
	audit_cl(4, "clone_thread_no_vm", bad, bad < 0 ? errno : 0);
	if (bad >= 0 || errno != EINVAL)
	{
		(void)munmap(stack, STACK_BYTES);
		return 1;
	}

	(void)munmap(stack, STACK_BYTES);
	(void)write(1, "[CLONEOK]\n", 10);
	return 0;
}
