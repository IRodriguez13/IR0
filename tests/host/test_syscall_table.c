/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness.h"

#include <ir0/errno.h>
#include <ir0/syscall_table.h>

static int64_t direct_handler(uint64_t a1, uint64_t a2, uint64_t a3,
			      uint64_t a4, uint64_t a5, uint64_t a6)
{
	return (int64_t)(a1 + a2 + a3 + a4 + a5 + a6);
}

static int64_t context_handler(void *opaque, enum ir0_syscall_id id,
			       uint64_t a1, uint64_t a2, uint64_t a3,
			       uint64_t a4, uint64_t a5, uint64_t a6)
{
	uint64_t bias = *(const uint64_t *)opaque;

	(void)a2;
	(void)a3;
	(void)a4;
	(void)a5;
	(void)a6;
	return (int64_t)(bias + (uint64_t)id + a1);
}

void test_syscall_handler_table_contract(void)
{
	struct syscall_handler_table table;
	uint64_t bias = 100;
	static const enum ir0_syscall_id provider_ids[] = {
		IR0_SYSCALL_OPENAT, IR0_SYSCALL_CLOSE,
	};
	static const enum ir0_syscall_id invalid_ids[] = {
		IR0_SYSCALL_READ, IR0_SYSCALL_COUNT,
	};
	const struct syscall_context_provider provider = {
		.ids = provider_ids, .count = 2, .handler = context_handler,
	};
	const struct syscall_context_provider invalid_provider = {
		.ids = invalid_ids, .count = 2, .handler = context_handler,
	};

	TEST_BEGIN("semantic syscall handler table contract");
	syscall_handlers_init(&table);
	ASSERT(syscall_handler_invoke(&table, 0, IR0_SYSCALL_READ,
				      1, 2, 3, 4, 5, 6) == -ENOSYS);
	ASSERT(syscall_handler_set(&table, IR0_SYSCALL_READ, direct_handler) == 0);
	ASSERT(syscall_handler_invoke(&table, 0, IR0_SYSCALL_READ,
				      1, 2, 3, 4, 5, 6) == 21);
	ASSERT(syscall_context_handler_set(&table, IR0_SYSCALL_WRITE,
					   context_handler) == 0);
	ASSERT(syscall_handler_invoke(&table, &bias, IR0_SYSCALL_WRITE,
				      7, 0, 0, 0, 0, 0) ==
	       (int64_t)(107 + IR0_SYSCALL_WRITE));
	ASSERT(syscall_handler_set(&table, IR0_SYSCALL_UNKNOWN, direct_handler) ==
	       -EINVAL);
	ASSERT(syscall_context_provider_register(&table, &provider) == 0);
	ASSERT(syscall_handler_invoke(&table, &bias, IR0_SYSCALL_OPENAT,
				      1, 0, 0, 0, 0, 0) ==
	       (int64_t)(101 + IR0_SYSCALL_OPENAT));
	ASSERT(syscall_context_provider_register(&table, &invalid_provider) ==
	       -EINVAL);
	/* Invalid batch registration is transactional: READ remains direct. */
	ASSERT(syscall_handler_invoke(&table, 0, IR0_SYSCALL_READ,
				      1, 2, 3, 4, 5, 6) == 21);
	ASSERT(syscall_handler_invoke(&table, 0, IR0_SYSCALL_COUNT,
				      0, 0, 0, 0, 0, 0) == -ENOSYS);
	TEST_END();
}
