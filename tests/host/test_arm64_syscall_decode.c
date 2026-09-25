/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness.h"

#include <ir0/syscall_id.h>

void test_arm64_syscall_decode_contract(void)
{
	TEST_BEGIN("arm64 syscall ABI decoder contract");
	ASSERT(syscall_decode_number(63) == IR0_SYSCALL_READ);
	ASSERT(syscall_decode_number(64) == IR0_SYSCALL_WRITE);
	ASSERT(syscall_decode_number(93) == IR0_SYSCALL_EXIT);
	ASSERT(syscall_decode_number(172) == IR0_SYSCALL_GETPID);
	ASSERT(syscall_decode_number(220) == IR0_SYSCALL_CLONE);
	ASSERT(syscall_decode_number(139) == IR0_SYSCALL_RT_SIGRETURN);
	ASSERT(syscall_decode_number(222) == IR0_SYSCALL_MMAP);
	ASSERT(syscall_decode_number(293) == IR0_SYSCALL_RSEQ);
	ASSERT(syscall_decode_number(0) == IR0_SYSCALL_UNKNOWN);
	ASSERT(syscall_decode_number(UINT64_MAX) == IR0_SYSCALL_UNKNOWN);
	TEST_END();
}
