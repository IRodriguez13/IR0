/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: syscall_table.c
 * Description: Semantic native syscall handler table implementation.
 */

#include <ir0/syscall_table.h>
#include <ir0/errno.h>

void syscall_handlers_init(struct syscall_handler_table *table)
{
	unsigned int id;

	if (!table)
		return;
	for (id = 0; id < IR0_SYSCALL_COUNT; id++)
	{
		table->handlers[id] = 0;
		table->context_handlers[id] = 0;
	}
}

int syscall_handler_set(struct syscall_handler_table *table,
			enum ir0_syscall_id id, syscall_handler_t handler)
{
	if (!table || !handler || id <= IR0_SYSCALL_UNKNOWN ||
	    id >= IR0_SYSCALL_COUNT)
		return -EINVAL;
	table->handlers[id] = handler;
	table->context_handlers[id] = 0;
	return 0;
}

int syscall_context_handler_set(struct syscall_handler_table *table,
				enum ir0_syscall_id id,
				syscall_context_handler_t handler)
{
	if (!table || !handler || id <= IR0_SYSCALL_UNKNOWN ||
	    id >= IR0_SYSCALL_COUNT)
		return -EINVAL;
	table->handlers[id] = 0;
	table->context_handlers[id] = handler;
	return 0;
}

int64_t syscall_handler_invoke(const struct syscall_handler_table *table,
			       void *context, enum ir0_syscall_id id, uint64_t arg1,
			       uint64_t arg2, uint64_t arg3, uint64_t arg4,
			       uint64_t arg5, uint64_t arg6)
{
	syscall_handler_t handler;

	if (!table || id <= IR0_SYSCALL_UNKNOWN || id >= IR0_SYSCALL_COUNT)
		return -ENOSYS;
	handler = table->handlers[id];
	if (table->context_handlers[id])
		return table->context_handlers[id](context, id, arg1, arg2, arg3,
					   arg4, arg5, arg6);
	if (!handler)
		return -ENOSYS;
	return handler(arg1, arg2, arg3, arg4, arg5, arg6);
}
