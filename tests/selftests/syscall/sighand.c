/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: sighand.c
 * Description: clone(CLONE_SIGHAND) shares one handler table. Without the
 *              flag the child gets a copy. A later sigaction is visible
 *              across the share and stays private across the copy.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#else
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "ir0/ksft.h"

#ifndef CLONE_VM
#define CLONE_VM 0x100UL
#endif
#ifndef CLONE_VFORK
#define CLONE_VFORK 0x4000UL
#endif
#ifndef CLONE_SIGHAND
#define CLONE_SIGHAND 0x800UL
#endif

static void handler_keep(int sig)
{
	(void)sig;
}

static void handler_new(int sig)
{
	(void)sig;
}

static long ir0_clone(unsigned long flags)
{
#ifdef IR0_SELFTEST_NOLIBC
	return syscall(SYS_clone, (long)(flags | SIGCHLD), 0, 0, 0);
#else
	return syscall(SYS_clone, flags | SIGCHLD, 0, 0, 0, 0);
#endif
}

static int install(void (*handler)(int))
{
	struct sigaction sa;

#ifdef IR0_SELFTEST_NOLIBC
	sa.sa_handler = handler;
	sa.sa_flags = 0;
	sa.sa_restorer = 0;
	sa.sa_mask = 0;
#else
	__builtin_memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handler;
#endif
	return sigaction(SIGUSR1, &sa, 0);
}

static int read_handler(void (**out)(int))
{
	struct sigaction old;

	if (sigaction(SIGUSR1, 0, &old) != 0)
		return -1;
	*out = old.sa_handler;
	return 0;
}

static int reap(int pid, int *status)
{
#ifdef IR0_SELFTEST_NOLIBC
	return wait4(pid, status, 0, 0);
#else
	return waitpid(pid, status, 0);
#endif
}

/*
 * Parent installs handler_keep, the child installs handler_new, then the
 * parent reads its own table. @want is handler_keep for a copy and
 * handler_new when CLONE_SIGHAND shared the table.
 */
static int run_case(unsigned long flags, void (*want)(int))
{
	long pid;
	int status = 0;
	void (*got)(int) = 0;

	if (install(handler_keep) != 0)
		return 0;
	pid = ir0_clone(flags);
	if (pid < 0)
	{
		ksft_print_num("clone_errno", errno);
		return 0;
	}
	if (pid == 0)
	{
		if (install(handler_new) != 0)
			_exit(2);
		_exit(0);
	}
	if (reap((int)pid, &status) < 0)
		return 0;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
	{
		ksft_print_num("child_status", status);
		return 0;
	}
	if (read_handler(&got) != 0)
		return 0;
	return got == want;
}

static volatile int g_shared_flag;

static void child_share(void)
{
	g_shared_flag = 2;
	if (install(handler_new) != 0)
		_exit(2);
	_exit(0);
}

/*
 * The child starts at the instruction after the syscall, on @sp, and
 * calls the function pointer stored there. It must not return onto the
 * parent's frame: CLONE_VM shares that stack.
 */
static long clone_on_stack(unsigned long flags, void *sp)
{
	long ret;

#if defined(__x86_64__)
	__asm__ volatile(
		"xor %%r10, %%r10\n\t"
		"xor %%r8, %%r8\n\t"
		"syscall\n\t"
		"test %%rax, %%rax\n\t"
		"jnz 1f\n\t"
		"pop %%rax\n\t"
		"call *%%rax\n\t"
		"mov $60, %%eax\n\t"
		"xor %%edi, %%edi\n\t"
		"syscall\n\t"
		"1:\n\t"
		: "=a"(ret)
		: "0"(56L), "D"(flags), "S"(sp), "d"(0L)
		: "rcx", "r8", "r10", "r11", "memory");
#elif defined(__aarch64__)
	register long x0 __asm__("x0") = (long)flags;
	register long x1 __asm__("x1") = (long)sp;
	register long x8 __asm__("x8") = 220L;

	__asm__ volatile(
		"mov x2, xzr\n\t"
		"mov x3, xzr\n\t"
		"mov x4, xzr\n\t"
		"svc #0\n\t"
		"cbnz x0, 1f\n\t"
		"ldr x9, [sp]\n\t"
		"blr x9\n\t"
		"mov x0, #0\n\t"
		"mov x8, #93\n\t"
		"svc #0\n\t"
		"1:\n\t"
		: "+r"(x0)
		: "r"(x1), "r"(x8)
		: "x2", "x3", "x4", "x9", "x30", "memory");
	ret = x0;
#else
	(void)flags;
	(void)sp;
	ret = -1;
#endif
	if (ret < 0)
	{
		errno = (int)(-ret);
		return -1;
	}
	return ret;
}

/* CLONE_VM|CLONE_SIGHAND: the handler and a store are both visible. */
static int run_shared(void)
{
	unsigned long *sp;
	long pid;
	int status = 0;
	void (*got)(int) = 0;
	void *map;

#ifdef IR0_SELFTEST_NOLIBC
	map = mmap(0, 16384, 3, 0x22, -1, 0);
#else
	map = mmap(0, 16384, 3, 0x22, -1, 0);
#endif
	if (map == (void *)-1)
		return 0;
	sp = (unsigned long *)((char *)map + 16384);
	sp -= 2;
	sp[0] = (unsigned long)child_share;
	if (install(handler_keep) != 0)
		return 0;
	g_shared_flag = 1;
	pid = clone_on_stack(CLONE_VM | CLONE_SIGHAND | SIGCHLD, sp);
	if (pid < 0)
	{
		ksft_print_num("clone_errno", errno);
		return 0;
	}
	if (pid == 0)
		_exit(3);
	if (reap((int)pid, &status) < 0)
		return 0;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
	{
		ksft_print_num("child_status", status);
		return 0;
	}
	if (read_handler(&got) != 0)
		return 0;
	return got == handler_new && g_shared_flag == 2;
}

/* Linux copy_process() rejects CLONE_SIGHAND unless CLONE_VM is set. */
static int sighand_without_vm_is_einval(void)
{
	long pid;

	pid = ir0_clone(CLONE_SIGHAND);
	if (pid == 0)
		_exit(0);
	if (pid > 0)
	{
		int status = 0;

		(void)reap((int)pid, &status);
		return 0;
	}
#ifdef IR0_SELFTEST_NOLIBC
	return errno == 22;
#else
	return errno == EINVAL;
#endif
}

int main(void)
{
	ksft_set_plan(3);
	ksft_test_result(run_case(0, handler_keep), "sighand_copied");
	ksft_test_result(run_shared(), "sighand_shared");
	ksft_test_result(sighand_without_vm_is_einval(), "sighand_needs_vm");
	return ksft_exit();
}
