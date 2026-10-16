/* SPDX-License-Identifier: GPL-3.0-only */
/** Architecture-neutral descriptor semantics for staged console userspace. */

#include <stdint.h>
#include <ir0/console.h>
#include <ir0/early_io.h>
#include <ir0/early_rootfs.h>
#include <ir0/early_syscall.h>
#include <ir0/time.h>

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

	if (early_rootfs_fd_is_pipe_write((int)descriptor))
		return early_rootfs_pipe_write((int)descriptor, buffer, length);
	if (early_rootfs_fd_is_block((int)descriptor))
		return early_rootfs_block_write((int)descriptor, buffer, length);
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

#define PPOLL_MAX 16
#define PSELECT_FD_BYTES 16
#define NS_PER_SEC 1000000000ULL

static int64_t early_sleep_timeout(struct early_syscall_context *context,
				   uint64_t timeout)
{
	const struct early_syscall_services *services_all;
	const struct early_user_services *user;
	const struct early_time_services *ops;
	struct timespec value;
	uint64_t frequency, seconds_ticks, nanosecond_ticks, delta, start;

	if (!timeout)
		return 0;
	if (!context || !(services_all = context->services) ||
	    !(user = services_all->user) || !user->copy_from_user ||
	    !(ops = services_all->time) || !ops->counter_read ||
	    !ops->counter_frequency || !ops->cpu_relax)
		return 0;
	if (user->copy_from_user(&value, timeout, sizeof(value)) != 0)
		return -EFAULT;
	if (value.tv_sec < 0 || value.tv_nsec < 0 ||
	    value.tv_nsec >= (int64_t)NS_PER_SEC)
		return -EINVAL;
	frequency = ops->counter_frequency();
	if (!frequency)
		return 0;
	seconds_ticks = (uint64_t)value.tv_sec * frequency;
	nanosecond_ticks = ((uint64_t)value.tv_nsec * frequency) / NS_PER_SEC;
	delta = seconds_ticks + nanosecond_ticks;
	if (!delta)
		return 0;
	start = ops->counter_read();
	while (ops->counter_read() - start < delta)
		ops->cpu_relax();
	return 0;
}

static int64_t early_ppoll(struct early_syscall_context *context,
			   uint64_t fds, uint64_t nfds, uint64_t timeout)
{
	const struct early_user_services *user;
	uint64_t i;
	int ready = 0;
	int64_t sleep_ret;

	if (nfds > PPOLL_MAX)
		return -EINVAL;
	if (!context || !context->services ||
	    !(user = context->services->user) || !user->copy_from_user ||
	    !user->copy_to_user)
		return -ENOSYS;
	for (i = 0; i < nfds; i++)
	{
		int32_t fd;
		int16_t ev[2];
		int revents;

		if (user->copy_from_user(&fd, fds + i * 8UL, sizeof(fd)) != 0 ||
		    user->copy_from_user(ev, fds + i * 8UL + 4UL, sizeof(ev)) != 0)
			return -EFAULT;
		revents = early_rootfs_poll_revents(fd, ev[0]);
		ev[1] = (int16_t)revents;
		if (user->copy_to_user(fds + i * 8UL + 6UL, &ev[1],
				       sizeof(ev[1])) != 0)
			return -EFAULT;
		if (revents)
			ready++;
	}
	if (ready)
		return ready;
	/*
	 * Linux ppoll NULL timeout waits forever. Staged EL0 has no waiter
	 * wake (no clone, no console input queue), so that would hang.
	 * A zero or missing timeout returns 0; a positive timeout sleeps
	 * then returns 0 if still idle.
	 */
	if (!timeout)
		return 0;
	sleep_ret = early_sleep_timeout(context, timeout);
	if (sleep_ret < 0)
		return sleep_ret;
	return 0;
}

static int fdset_bit(const uint8_t *set, unsigned fd)
{
	return (set[fd / 8] >> (fd % 8)) & 1;
}

static void fdset_set(uint8_t *set, unsigned fd)
{
	set[fd / 8] |= (uint8_t)(1U << (fd % 8));
}

static int64_t early_pselect6(struct early_syscall_context *context,
			      uint64_t nfds, uint64_t readfds, uint64_t writefds,
			      uint64_t exceptfds, uint64_t timeout)
{
	const struct early_user_services *user;
	uint8_t rin[PSELECT_FD_BYTES];
	uint8_t win[PSELECT_FD_BYTES];
	uint8_t ein[PSELECT_FD_BYTES];
	uint8_t rout[PSELECT_FD_BYTES];
	uint8_t wout[PSELECT_FD_BYTES];
	uint8_t eout[PSELECT_FD_BYTES];
	unsigned i;
	int ready = 0;
	int64_t sleep_ret;
	unsigned copy_n;

	if (nfds > 64)
		return -EINVAL;
	if (!context || !context->services ||
	    !(user = context->services->user) || !user->copy_from_user ||
	    !user->copy_to_user)
		return -ENOSYS;
	for (i = 0; i < PSELECT_FD_BYTES; i++)
	{
		rin[i] = win[i] = ein[i] = 0;
		rout[i] = wout[i] = eout[i] = 0;
	}
	copy_n = (unsigned)((nfds + 7) / 8);
	if (copy_n > PSELECT_FD_BYTES)
		copy_n = PSELECT_FD_BYTES;
	if (readfds && user->copy_from_user(rin, readfds, copy_n) != 0)
		return -EFAULT;
	if (writefds && user->copy_from_user(win, writefds, copy_n) != 0)
		return -EFAULT;
	if (exceptfds && user->copy_from_user(ein, exceptfds, copy_n) != 0)
		return -EFAULT;
	for (i = 0; i < (unsigned)nfds; i++)
	{
		int events = 0;
		int revents;

		if (readfds && fdset_bit(rin, i))
			events |= IR0_EARLY_POLLIN;
		if (writefds && fdset_bit(win, i))
			events |= IR0_EARLY_POLLOUT;
		if (exceptfds && fdset_bit(ein, i))
			events |= IR0_EARLY_POLLPRI;
		if (!events)
			continue;
		revents = early_rootfs_poll_revents((int)i, events);
		if ((revents & IR0_EARLY_POLLIN) && readfds && fdset_bit(rin, i))
		{
			fdset_set(rout, i);
			ready++;
		}
		if ((revents & IR0_EARLY_POLLOUT) && writefds &&
		    fdset_bit(win, i))
		{
			fdset_set(wout, i);
			ready++;
		}
		if ((revents & (IR0_EARLY_POLLERR | IR0_EARLY_POLLPRI)) &&
		    exceptfds && fdset_bit(ein, i))
		{
			fdset_set(eout, i);
			ready++;
		}
	}
	if (ready)
	{
		if (readfds &&
		    user->copy_to_user(readfds, rout, copy_n) != 0)
			return -EFAULT;
		if (writefds &&
		    user->copy_to_user(writefds, wout, copy_n) != 0)
			return -EFAULT;
		if (exceptfds &&
		    user->copy_to_user(exceptfds, eout, copy_n) != 0)
			return -EFAULT;
		return ready;
	}
	if (timeout)
	{
		sleep_ret = early_sleep_timeout(context, timeout);
		if (sleep_ret < 0)
			return sleep_ret;
	}
	if (readfds && user->copy_to_user(readfds, rout, copy_n) != 0)
		return -EFAULT;
	if (writefds && user->copy_to_user(writefds, wout, copy_n) != 0)
		return -EFAULT;
	if (exceptfds && user->copy_to_user(exceptfds, eout, copy_n) != 0)
		return -EFAULT;
	return 0;
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
	case IR0_SYSCALL_PIPE2:
		return early_rootfs_pipe2(a0, (int)a1);
	case IR0_SYSCALL_PPOLL:
		(void)a3;
		(void)a4;
		return early_ppoll(opaque, a0, a1, a2);
	case IR0_SYSCALL_PSELECT6:
		return early_pselect6(opaque, a0, a1, a2, a3, a4);
	default:
		return -ENOSYS;
	}
}

static const enum ir0_syscall_id g_io_syscalls[] = {
	IR0_SYSCALL_WRITE, IR0_SYSCALL_WRITEV, IR0_SYSCALL_READV,
	IR0_SYSCALL_IOCTL, IR0_SYSCALL_FCNTL, IR0_SYSCALL_PIPE2,
	IR0_SYSCALL_DUP, IR0_SYSCALL_DUP3, IR0_SYSCALL_PPOLL,
	IR0_SYSCALL_PSELECT6,
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
