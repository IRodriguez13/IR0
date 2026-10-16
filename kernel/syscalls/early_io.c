/* SPDX-License-Identifier: GPL-3.0-only */
/** Architecture-neutral descriptor semantics for staged console userspace. */

#include <stdint.h>
#include <ir0/console.h>
#include <ir0/early_io.h>
#include <ir0/early_rootfs.h>
#include <ir0/early_syscall.h>

#define EBADF 9
#define EFAULT 14
#define EINVAL 22
#define ENOSYS 38
#define ENOTTY 25
#define WRITE_MAX 256UL

static int g_write_ok;
static int g_termios_ready;
static struct ir0_termios g_termios;
static int32_t g_fg_pgid = 1;

static void early_termios_ensure(void)
{
	unsigned i;
	unsigned char *raw;

	if (g_termios_ready)
		return;
	raw = (unsigned char *)&g_termios;
	for (i = 0; i < sizeof(g_termios); i++)
		raw[i] = 0;
	g_termios.c_iflag = IR0_CONSOLE_IFLAG_DEFAULT;
	g_termios.c_oflag = IR0_CONSOLE_OFLAG_DEFAULT;
	g_termios.c_cflag = IR0_CONSOLE_CFLAG_DEFAULT;
	g_termios.c_lflag = IR0_CONSOLE_LFLAG_DEFAULT;
	g_termios.c_line = 0;
	g_termios.c_cc[IR0_CC_VINTR] = 3;
	g_termios.c_cc[IR0_CC_VQUIT] = 28;
	g_termios.c_cc[IR0_CC_VERASE] = 127;
	g_termios.c_cc[IR0_CC_VEOF] = 4;
	g_termios.c_cc[IR0_CC_VTIME] = 0;
	g_termios.c_cc[IR0_CC_VMIN] = 1;
	g_termios_ready = 1;
}

static int64_t early_write_discard(struct early_syscall_context *context,
				   uint64_t buffer, uint64_t length)
{
	const struct early_user_services *user;
	char bytes[WRITE_MAX];
	uint64_t done = 0;
	uint64_t count;

	if (!length)
		return 0;
	if (!context || !context->services ||
	    !(user = context->services->user) || !user->copy_from_user)
		return -ENOSYS;
	while (done < length)
	{
		count = (length - done) > WRITE_MAX ? WRITE_MAX : (length - done);
		if (user->copy_from_user(bytes, buffer + done, count) != 0)
			return done ? (int64_t)done : -EFAULT;
		done += count;
	}
	return (int64_t)done;
}

static int64_t early_write(struct early_syscall_context *context,
			   uint64_t descriptor, uint64_t buffer, uint64_t length)
{
	const struct early_user_services *user;
	const struct early_console_services *console;
	char bytes[WRITE_MAX];
	uint64_t count, index;

	if (early_rootfs_fd_discards_write((int)descriptor))
		return early_write_discard(context, buffer, length);
	if (!early_rootfs_fd_is_console((int)descriptor))
		return -EBADF;
	if (!length)
		return 0;
	if (!context || !context->services ||
	    !(user = context->services->user) ||
	    !(console = context->services->console) ||
	    !user->copy_from_user || !console->console_putc)
		return -ENOSYS;
	count = length > WRITE_MAX ? WRITE_MAX : length;
	if (user->copy_from_user(bytes, buffer, count) != 0)
		return -EFAULT;
	for (index = 0; index < count; index++) {
		if (bytes[index] == '\n')
			console->console_putc('\r');
		console->console_putc(bytes[index]);
	}
	if (console->console_write_observed)
		console->console_write_observed(bytes, count);
	g_write_ok = 1;
	return (int64_t)count;
}

static int64_t early_ioctl(struct early_syscall_context *context,
			   uint64_t descriptor, uint64_t request, uint64_t arg)
{
	const struct early_user_services *user;
	struct ir0_winsize winsize;
	int32_t pgid;

	if (!early_rootfs_fd_is_console((int)descriptor))
		return -ENOTTY;
	if (!context || !context->services ||
	    !(user = context->services->user) ||
	    !user->copy_to_user || !user->copy_from_user)
		return -ENOSYS;
	early_termios_ensure();
	if (request == IR0_CONSOLE_TCGETS)
	{
		if (!arg)
			return -EFAULT;
		return user->copy_to_user(arg, &g_termios, sizeof(g_termios)) == 0 ?
			0 : -EFAULT;
	}
	if (request == IR0_CONSOLE_TCSETS ||
	    request == IR0_CONSOLE_TCSETSW ||
	    request == IR0_CONSOLE_TCSETSF)
	{
		if (!arg)
			return -EFAULT;
		return user->copy_from_user(&g_termios, arg, sizeof(g_termios)) == 0 ?
			0 : -EFAULT;
	}
	if (request == IR0_CONSOLE_TIOCGWINSZ)
	{
		if (!arg)
			return -EFAULT;
		winsize.ws_row = 25;
		winsize.ws_col = 80;
		winsize.ws_xpixel = 0;
		winsize.ws_ypixel = 0;
		return user->copy_to_user(arg, &winsize, sizeof(winsize)) == 0 ?
			0 : -EFAULT;
	}
	if (request == IR0_TIOCGPGRP)
	{
		if (!arg)
			return -EFAULT;
		return user->copy_to_user(arg, &g_fg_pgid, sizeof(g_fg_pgid)) == 0 ?
			0 : -EFAULT;
	}
	if (request == IR0_TIOCSPGRP)
	{
		if (!arg)
			return -EFAULT;
		if (user->copy_from_user(&pgid, arg, sizeof(pgid)) != 0)
			return -EFAULT;
		if (pgid != 1)
			return -EINVAL;
		g_fg_pgid = pgid;
		return 0;
	}
	if (request == IR0_TIOCSCTTY)
		return 0;
	if (request == IR0_CONSOLE_FIONREAD)
	{
		int zero = 0;

		if (!arg)
			return -EFAULT;
		return user->copy_to_user(arg, &zero, sizeof(zero)) == 0 ?
			0 : -EFAULT;
	}
	return -ENOTTY;
}

static int64_t early_writev(struct early_syscall_context *context,
			    uint64_t descriptor, uint64_t iov, uint64_t iovcnt)
{
	const struct early_user_services *user;
	uint64_t i;
	int64_t total = 0;

	if (iovcnt == 0)
		return 0;
	if (iovcnt > 8)
		return -EINVAL;
	if (!context || !context->services ||
	    !(user = context->services->user) || !user->copy_from_user)
		return -ENOSYS;
	for (i = 0; i < iovcnt; i++)
	{
		uint64_t vec[2];
		int64_t n;

		if (user->copy_from_user(vec, iov + i * 16UL, sizeof(vec)) != 0)
			return total ? total : -EFAULT;
		if (vec[1] == 0)
			continue;
		n = early_write(context, descriptor, vec[0], vec[1]);
		if (n < 0)
			return total ? total : n;
		total += n;
		if ((uint64_t)n < vec[1])
			break;
	}
	return total;
}

static int64_t early_readv(struct early_syscall_context *context,
			   uint64_t descriptor, uint64_t iov, uint64_t iovcnt)
{
	const struct early_user_services *user;
	uint64_t i;
	int64_t total = 0;

	if (iovcnt == 0)
		return 0;
	if (iovcnt > 8)
		return -EINVAL;
	if (!context || !context->services ||
	    !(user = context->services->user) || !user->copy_from_user)
		return -ENOSYS;
	for (i = 0; i < iovcnt; i++)
	{
		uint64_t vec[2];
		int64_t n;

		if (user->copy_from_user(vec, iov + i * 16UL, sizeof(vec)) != 0)
			return total ? total : -EFAULT;
		if (vec[1] == 0)
			continue;
		n = early_rootfs_read((int)descriptor, vec[0], vec[1]);
		if (n < 0)
			return total ? total : n;
		total += n;
		if ((uint64_t)n < vec[1])
			break;
	}
	return total;
}

static int64_t early_io_syscall(void *opaque, enum ir0_syscall_id id,
				uint64_t a0, uint64_t a1, uint64_t a2,
				uint64_t a3, uint64_t a4, uint64_t a5)
{
	(void)a3; (void)a4; (void)a5;
	switch (id) {
	case IR0_SYSCALL_WRITE:
		return early_write(opaque, a0, a1, a2);
	case IR0_SYSCALL_WRITEV:
		return early_writev(opaque, a0, a1, a2);
	case IR0_SYSCALL_READV:
		return early_readv(opaque, a0, a1, a2);
	case IR0_SYSCALL_IOCTL:
		return early_ioctl(opaque, a0, a1, a2);
	case IR0_SYSCALL_DUP:
		return early_rootfs_dup((int)a0);
	case IR0_SYSCALL_DUP3:
		return early_rootfs_dup3((int)a0, (int)a1, (int)a2);
	case IR0_SYSCALL_FCNTL:
		return early_rootfs_fcntl((int)a0, (int)a1, a2);
	case IR0_SYSCALL_PPOLL:
	default:
		return -ENOSYS;
	}
}

static const enum ir0_syscall_id g_io_syscalls[] = {
	IR0_SYSCALL_WRITE, IR0_SYSCALL_WRITEV, IR0_SYSCALL_READV,
	IR0_SYSCALL_IOCTL, IR0_SYSCALL_FCNTL,
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
