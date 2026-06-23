/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_signal_rt_sigaction_abi.c
 * Description: Host ABI checks for rt_sigaction / siginfo layout (no kernel signals.h)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define IR0_SIGSEGV      11
#define IR0_SA_SIGINFO   4
#define IR0_SA_RESTORER  0x04000000U
#define IR0_SA_RESTART   0x10000000U
#define IR0_SEGV_MAPERR  1
#define IR0_SIGSET_BYTES 128
#define IR0_SIGINFO_BYTES 128
#define IR0_SIGSET_WORDS 16

typedef struct
{
	unsigned long __val[IR0_SIGSET_WORDS];
} ir0_sigset_t;

/*
 * Linux x86-64 kernel uapi layout (matches includes/ir0/signals.h).
 * musl builds this on the stack for the rt_sigaction syscall when sigsetsize=8.
 */
typedef struct
{
	void (*sa_handler)(int);
	unsigned long sa_flags;
	void (*sa_restorer)(void);
	ir0_sigset_t sa_mask;
} ir0_kernel_sigaction_t;

static uint32_t host_rt_sigaction_mask_from_sigset(const ir0_sigset_t *set,
						   size_t sigsetsize)
{
	uint64_t legacy64;

	if (sigsetsize == sizeof(uint64_t))
	{
		if (!set)
			return 0;
		memcpy(&legacy64, set, sizeof(legacy64));
		return (uint32_t)legacy64;
	}
	if (!set)
		return 0;
	return (uint32_t)(set->__val[0] & 0xFFFFFFFFUL);
}

static void host_rt_sigaction_mask_to_sigset(ir0_sigset_t *set, size_t sigsetsize,
					     uint32_t mask)
{
	uint64_t legacy64;
	size_t i;

	if (!set)
		return;

	if (sigsetsize == sizeof(uint64_t))
	{
		legacy64 = (uint64_t)mask;
		memcpy(set, &legacy64, sizeof(legacy64));
		return;
	}

	for (i = 0; i < IR0_SIGSET_WORDS; i++)
		set->__val[i] = 0;
	set->__val[0] = (unsigned long)mask;
}

static int host_rt_sigaction_sigsetsize_valid(size_t sigsetsize)
{
	return (sigsetsize == IR0_SIGSET_BYTES ||
		sigsetsize == sizeof(uint64_t));
}

static size_t host_rt_sigaction_user_copy_size(size_t sigsetsize)
{
	if (sigsetsize == sizeof(uint64_t))
		return offsetof(ir0_kernel_sigaction_t, sa_mask) + sizeof(uint64_t);
	return sizeof(ir0_kernel_sigaction_t);
}

void test_signal_rt_sigaction_abi(void)
{
	ir0_sigset_t set;
	uint32_t mask;
	ir0_kernel_sigaction_t kact;
	uint8_t stack_buf[40];
	uint8_t poison = 0xA5;

	TEST_BEGIN("signal_rt_sigaction_abi");

	ASSERT_EQ(IR0_SIGSEGV, 11);
	ASSERT_EQ(IR0_SA_SIGINFO, 4);
	ASSERT_EQ(IR0_SEGV_MAPERR, 1);
	ASSERT_EQ(IR0_SIGSET_BYTES, 128);
	ASSERT_EQ(IR0_SIGINFO_BYTES, 128);
	ASSERT(sizeof(void *) == 8);

	ASSERT_EQ(offsetof(ir0_kernel_sigaction_t, sa_handler), 0U);
	ASSERT_EQ(offsetof(ir0_kernel_sigaction_t, sa_flags), 8U);
	ASSERT_EQ(offsetof(ir0_kernel_sigaction_t, sa_restorer), 16U);
	ASSERT_EQ(offsetof(ir0_kernel_sigaction_t, sa_mask), 24U);
	ASSERT_EQ(sizeof(ir0_kernel_sigaction_t), 152U);

	ASSERT(host_rt_sigaction_sigsetsize_valid(sizeof(uint64_t)));
	ASSERT(host_rt_sigaction_sigsetsize_valid(IR0_SIGSET_BYTES));
	ASSERT(!host_rt_sigaction_sigsetsize_valid(16));
	ASSERT(!host_rt_sigaction_sigsetsize_valid(0));

	ASSERT_EQ(host_rt_sigaction_user_copy_size(sizeof(uint64_t)), 32U);
	ASSERT_EQ(host_rt_sigaction_user_copy_size(IR0_SIGSET_BYTES),
		  sizeof(ir0_kernel_sigaction_t));

	memset(&set, 0, sizeof(set));
	{
		uint64_t legacy = 0x0000000000000800ULL;

		memcpy(&set, &legacy, sizeof(legacy));
	}
	mask = host_rt_sigaction_mask_from_sigset(&set, sizeof(uint64_t));
	ASSERT_EQ(mask, 0x800U);

	host_rt_sigaction_mask_to_sigset(&set, sizeof(uint64_t), 0x400U);
	mask = host_rt_sigaction_mask_from_sigset(&set, sizeof(uint64_t));
	ASSERT_EQ(mask, 0x400U);

	host_rt_sigaction_mask_to_sigset(&set, IR0_SIGSET_BYTES, 0x200U);
	mask = host_rt_sigaction_mask_from_sigset(&set, IR0_SIGSET_BYTES);
	ASSERT_EQ(mask, 0x200U);

	memset(stack_buf, poison, sizeof(stack_buf));
	memset(&kact, 0, sizeof(kact));
	kact.sa_handler = (void (*)(int))0x426AF5UL;
	kact.sa_flags = IR0_SA_RESTORER;
	kact.sa_restorer = (void (*)(void))0x4422B6UL;
	host_rt_sigaction_mask_to_sigset(&kact.sa_mask, sizeof(uint64_t), 0x800U);
	memcpy(stack_buf, &kact, host_rt_sigaction_user_copy_size(sizeof(uint64_t)));
	ASSERT_EQ(stack_buf[32], poison);
	ASSERT_EQ(stack_buf[39], poison);

	/* Round-trip oldact: kernel uapi compact write, musl reads handler/flags/mask. */
	{
		ir0_kernel_sigaction_t old_out;
		void (*handler_in)(int);
		unsigned long flags_in;
		uint64_t mask64_in;

		memset(stack_buf, poison, sizeof(stack_buf));
		memset(&old_out, 0, sizeof(old_out));
		old_out.sa_handler = (void (*)(int))0xDEADBEEFUL;
		old_out.sa_flags = IR0_SA_RESTORER | IR0_SA_RESTART;
		old_out.sa_restorer = (void (*)(void))0xCAFEBABEUL;
		host_rt_sigaction_mask_to_sigset(&old_out.sa_mask, sizeof(uint64_t),
						 0x1000U);
		memcpy(stack_buf, &old_out,
		       host_rt_sigaction_user_copy_size(sizeof(uint64_t)));
		ASSERT_EQ(stack_buf[32], poison);

		memcpy(&handler_in, stack_buf, sizeof(handler_in));
		memcpy(&flags_in, stack_buf + 8, sizeof(flags_in));
		memcpy(&mask64_in, stack_buf + 24, sizeof(mask64_in));
		ASSERT_EQ((uintptr_t)handler_in, 0xDEADBEEFUL);
		ASSERT_EQ(flags_in, (unsigned long)(IR0_SA_RESTORER | IR0_SA_RESTART));
		ASSERT_EQ(mask64_in, 0x1000ULL);
	}

	TEST_END();
}
