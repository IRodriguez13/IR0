/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness.h"

#include <ir0/syscall_id.h>
#include <ir0/syscall_linux.h>

extern enum ir0_syscall_id x86_test_syscall_decode_number(uint64_t abi_number);

void test_x86_syscall_decode_contract(void)
{
	TEST_BEGIN("x86-64 syscall ABI decoder contract");
	ASSERT(x86_test_syscall_decode_number(__NR_read) == IR0_SYSCALL_READ);
	ASSERT(x86_test_syscall_decode_number(__NR_rt_sigreturn) ==
	       IR0_SYSCALL_RT_SIGRETURN);
	ASSERT(x86_test_syscall_decode_number(__NR_clone) == IR0_SYSCALL_CLONE);
	ASSERT(x86_test_syscall_decode_number(__NR_fork) == IR0_SYSCALL_FORK);
	ASSERT(x86_test_syscall_decode_number(__NR_clock_gettime) ==
	       IR0_SYSCALL_CLOCK_GETTIME);
	ASSERT(x86_test_syscall_decode_number(__NR_clock_gettime64) ==
	       IR0_SYSCALL_CLOCK_GETTIME);
	ASSERT(x86_test_syscall_decode_number(UINT64_MAX) == IR0_SYSCALL_UNKNOWN);
	TEST_END();
}
