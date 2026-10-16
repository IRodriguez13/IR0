/* SPDX-License-Identifier: GPL-3.0-only */
/** Architecture-neutral descriptor semantics for staged console userspace. */

#include <stdint.h>
#include <ir0/early_io.h>
#include <ir0/early_syscall.h>

#define EBADF 9
#define EFAULT 14
#define ENOSYS 38
#define ENOTTY 25
#define WRITE_MAX 256UL

static int g_write_ok;

static int64_t early_write(struct early_syscall_context *context,
			   uint64_t descriptor, uint64_t buffer, uint64_t length)
{
	const struct early_syscall_services *ops;
	char bytes[WRITE_MAX];
	uint64_t count, index;

	if (descriptor != 1 && descriptor != 2)
		return -EBADF;
	if (!length)
		return 0;
	if (!context || !(ops = context->services) || !ops->copy_from_user ||
	    !ops->console_putc)
		return -ENOSYS;
	count = length > WRITE_MAX ? WRITE_MAX : length;
	if (ops->copy_from_user(bytes, buffer, count) != 0)
		return -EFAULT;
	for (index = 0; index < count; index++) {
		if (bytes[index] == '\n')
			ops->console_putc('\r');
		ops->console_putc(bytes[index]);
	}
	if (ops->console_write_observed)
		ops->console_write_observed(bytes, count);
	g_write_ok = 1;
	return (int64_t)count;
}

static int64_t early_io_syscall(void *opaque, enum ir0_syscall_id id,
				uint64_t a0, uint64_t a1, uint64_t a2,
				uint64_t a3, uint64_t a4, uint64_t a5)
{
	(void)a3; (void)a4; (void)a5;
	switch (id) {
	case IR0_SYSCALL_WRITE:
		return early_write(opaque, a0, a1, a2);
	case IR0_SYSCALL_IOCTL:
		return -ENOTTY;
	case IR0_SYSCALL_FCNTL:
	case IR0_SYSCALL_PPOLL:
	case IR0_SYSCALL_DUP:
	case IR0_SYSCALL_DUP3:
	default:
		return -ENOSYS;
	}
}

static const enum ir0_syscall_id g_io_syscalls[] = {
	IR0_SYSCALL_WRITE, IR0_SYSCALL_IOCTL, IR0_SYSCALL_FCNTL,
	IR0_SYSCALL_DUP, IR0_SYSCALL_DUP3, IR0_SYSCALL_PPOLL,
};

const struct syscall_context_provider early_io_provider = {
	.ids = g_io_syscalls,
	.count = sizeof(g_io_syscalls) / sizeof(g_io_syscalls[0]),
	.handler = early_io_syscall,
};

int early_io_smoke_ok(void)
{
	return g_write_ok;
}
