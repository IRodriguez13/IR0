/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness.h"

#include <ir0/errno.h>
#include <ir0/syscall_table.h>

static int64_t direct_handler(uint64_t a1, uint64_t a2, uint64_t a3,
			      uint64_t a4, uint64_t a5, uint64_t a6)
{
	return (int64_t)(a1 + a2 + a3 + a4 + a5 + a6);
}

void test_syscall_handler_table_contract(void)
{
	struct syscall_handler_table table;

	TEST_BEGIN("semantic syscall handler table contract");
	syscall_handlers_init(&table);
	ASSERT(syscall_handler_invoke(&table, IR0_SYSCALL_READ,
				      1, 2, 3, 4, 5, 6) == -ENOSYS);
	ASSERT(syscall_handler_set(&table, IR0_SYSCALL_READ, direct_handler) == 0);
	ASSERT(syscall_handler_invoke(&table, IR0_SYSCALL_READ,
				      1, 2, 3, 4, 5, 6) == 21);
	ASSERT(syscall_handler_set(&table, IR0_SYSCALL_UNKNOWN, direct_handler) ==
	       -EINVAL);
	ASSERT(syscall_handler_set(&table, IR0_SYSCALL_COUNT, direct_handler) ==
	       -EINVAL);
	ASSERT(syscall_handler_set(&table, IR0_SYSCALL_WRITE, 0) == -EINVAL);
	/* An unset ID and an out-of-range ID both fail honestly. */
	ASSERT(syscall_handler_invoke(&table, IR0_SYSCALL_GETPID,
				      0, 0, 0, 0, 0, 0) == -ENOSYS);
	ASSERT(syscall_handler_invoke(&table, IR0_SYSCALL_COUNT,
				      0, 0, 0, 0, 0, 0) == -ENOSYS);
	TEST_END();
}
