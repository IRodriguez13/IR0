/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * Architecture-neutral dispatcher for staged userspace bring-up.
 * ABI number decoding and exception return remain architecture services.
 */

#include <ir0/early_syscall.h>

int early_syscall_dispatch_init(
	struct early_syscall_dispatch *dispatch,
	const struct syscall_context_provider *const *providers,
	unsigned int provider_count)
{
	unsigned int index;
	int result;

	if (!dispatch || (!providers && provider_count))
		return -1;
	if (dispatch->state)
		return dispatch->state == 1 ? 0 : dispatch->state;

	syscall_handlers_init(&dispatch->handlers);
	for (index = 0; index < provider_count; index++) {
		result = syscall_context_provider_register(&dispatch->handlers,
							  providers[index]);
		if (result != 0) {
			dispatch->state = result;
			return result;
		}
	}
	dispatch->state = 1;
	return 0;
}

int64_t early_syscall_dispatch_invoke(
	struct early_syscall_dispatch *dispatch,
	struct early_syscall_context *context,
	const struct syscall_context_provider *const *providers,
	unsigned int provider_count, enum ir0_syscall_id id,
	uint64_t a0, uint64_t a1, uint64_t a2,
	uint64_t a3, uint64_t a4, uint64_t a5)
{
	int result;

	result = early_syscall_dispatch_init(dispatch, providers, provider_count);
	if (result != 0)
		return result;
	return syscall_handler_invoke(&dispatch->handlers, context, id,
				      a0, a1, a2, a3, a4, a5);
}
