/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: sigreturn_bad_frame.c
 * Description: rt_sigreturn reads the signal frame only from the user stack
 *              pointer, through the uaccess checks, as on Linux. A frame
 *              that cannot be copied in ends the task with SIGSEGV; a
 *              context pointer left in the first argument register is never
 *              used instead, and a supervisor-only address is never read
 *              as a frame.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#endif

#include "ir0/ksft.h"

#ifndef IR0_SELFTEST_NOLIBC
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

/*
 * IR0 sigcontext slots for the resume IP and SP, and a supervisor-only
 * address inside the low user VA window (see uaccess_kernel_addr.c).
 */
#if defined(__x86_64__)
#define CTX_IP_SLOT 16
#define CTX_SP_SLOT 19
#define KERNEL_ADDR 0x500000UL
#elif defined(__aarch64__)
#define CTX_IP_SLOT 33
#define CTX_SP_SLOT 32
#define KERNEL_ADDR 0x40800000UL
#else
#error "sigreturn_bad_frame: unsupported ISA"
#endif

/* Below every user mapping: no frame can be copied from here. */
#define UNMAPPED_SP 0x1000UL
#define RESUMED_EXIT_CODE 42
#define CTX_WORDS 600

static unsigned long g_ctx[CTX_WORDS] __attribute__((aligned(16)));
static unsigned char g_stack[4096] __attribute__((aligned(16)));

static void resumed(void)
{
	_exit(RESUMED_EXIT_CODE);
}

/* rt_sigreturn with stack pointer @sp and @arg in the first argument. */
static void __attribute__((noreturn)) raw_sigreturn(unsigned long sp,
						    unsigned long arg)
{
#if defined(__x86_64__)
	__asm__ volatile("mov %0, %%rsp\n\t"
			 "mov %1, %%rdi\n\t"
			 "mov $15, %%eax\n\t"
			 "syscall"
			 :
			 : "r"(sp), "r"(arg)
			 : "memory");
#else
	__asm__ volatile("mov sp, %0\n\t"
			 "mov x0, %1\n\t"
			 "mov x8, #139\n\t"
			 "svc #0"
			 :
			 : "r"(sp), "r"(arg)
			 : "memory");
#endif
	for (;;)
		;
}

static int child_status(unsigned long sp, unsigned long arg)
{
	int status = 0;
	int pid = fork();

	if (pid < 0)
		return -1;
	if (pid == 0)
	{
		/*
		 * Written by the child, so the page is its own and writable:
		 * nothing but the kernel's refusal stops a resume through it.
		 */
		g_ctx[CTX_IP_SLOT] = (unsigned long)resumed;
		g_ctx[CTX_SP_SLOT] =
			(unsigned long)(g_stack + sizeof(g_stack) - 64);
		raw_sigreturn(sp, arg);
	}
	if (wait4(pid, &status, 0, 0) != pid)
		return -1;
	return status;
}

static int killed_by_sigsegv(int status)
{
	if (WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV)
		return 1;
	ksft_print_num("status", status);
	return 0;
}

/* A valid context in the first argument register does not stand in. */
static int register_context_ignored(void)
{
	return killed_by_sigsegv(child_status(UNMAPPED_SP,
					      (unsigned long)g_ctx));
}

/* A supervisor-only frame address is refused, not read by the kernel. */
static int kernel_frame_refused(void)
{
	return killed_by_sigsegv(child_status(KERNEL_ADDR, KERNEL_ADDR));
}

int main(void)
{
	ksft_set_plan(2);
	ksft_test_result(register_context_ignored(), "register_context_ignored");
	ksft_test_result(kernel_frame_refused(), "kernel_frame_refused");
	return ksft_exit();
}
