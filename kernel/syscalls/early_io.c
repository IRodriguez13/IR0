/* SPDX-License-Identifier: GPL-3.0-only */
/** Architecture-neutral descriptor semantics for staged console userspace. */

#include <stdint.h>
#include <ir0/console.h>
#include <ir0/early_io.h>
#include <ir0/early_process.h>
#include <ir0/early_rootfs.h>
#include <ir0/early_syscall.h>
#include <ir0/early_time.h>
#include <ir0/time.h>
#include <ir0/errno.h>

#define WRITE_MAX 256UL
#define CMSG_HDR 16U
#define CMSG_CAP 128U

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
	if (early_rootfs_fd_is_eventfd((int)descriptor))
		return early_rootfs_eventfd_write((int)descriptor, buffer, length);
	if (early_rootfs_fd_is_memfd((int)descriptor))
		return early_rootfs_memfd_write((int)descriptor, buffer, length);
	if (early_rootfs_fd_is_sock((int)descriptor))
		return early_rootfs_sock_write((int)descriptor, buffer, length);
	if (early_rootfs_fd_is_fifo((int)descriptor))
		return early_rootfs_fifo_write((int)descriptor, buffer, length);
	if (early_rootfs_fd_is_file((int)descriptor))
		return early_rootfs_file_write((int)descriptor, buffer, length);
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

	if (request == 0x80081272UL)
		return early_rootfs_blkgetsize64((int)descriptor, arg);
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

static int64_t early_preadv(struct early_syscall_context *context,
			    uint64_t descriptor, uint64_t iov, uint64_t iovcnt,
			    int64_t offset)
{
	const struct early_user_services *user;
	uint64_t i;
	int64_t total = 0;

	if (offset < 0)
		return -EINVAL;
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
		n = early_rootfs_pread64((int)descriptor, vec[0], vec[1],
					 offset + total);
		if (n < 0)
			return total ? total : n;
		total += n;
		if ((uint64_t)n < vec[1])
			break;
	}
	return total;
}

static int64_t early_pwritev(struct early_syscall_context *context,
			     uint64_t descriptor, uint64_t iov, uint64_t iovcnt,
			     int64_t offset)
{
	const struct early_user_services *user;
	uint64_t i;
	int64_t total = 0;

	if (offset < 0)
		return -EINVAL;
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
		n = early_rootfs_pwrite64((int)descriptor, vec[0], vec[1],
					  offset + total);
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
	 * Linux ppoll NULL timeout waits forever. With a cloned peer the
	 * current task yields and the SVC restarts after the peer runs.
	 * A lone task still returns 0 rather than hang.
	 */
	(void)early_process_request_yield();
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
	(void)early_process_request_yield();
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

static void early_io_note_clock(struct early_syscall_context *context)
{
	const struct early_syscall_services *all;
	const struct early_time_services *ops;

	if (!context || !(all = context->services) || !(ops = all->time) ||
	    !ops->counter_read || !ops->counter_frequency)
		return;
	early_rootfs_note_clock(ops->counter_read(), ops->counter_frequency(),
				early_time_realtime_offset());
}

static void early_io_sleep_ms(struct early_syscall_context *context, int ms)
{
	const struct early_syscall_services *all;
	const struct early_time_services *ops;
	uint64_t hz;
	uint64_t start;
	uint64_t delta;

	if (ms <= 0 || !context || !(all = context->services) ||
	    !(ops = all->time) || !ops->counter_read || !ops->counter_frequency ||
	    !ops->cpu_relax)
		return;
	hz = ops->counter_frequency();
	if (!hz)
		return;
	delta = ((uint64_t)ms * hz) / 1000ULL;
	if (!delta)
		delta = 1;
	start = ops->counter_read();
	while (ops->counter_read() - start < delta)
		ops->cpu_relax();
}

static int64_t early_epoll_pwait(struct early_syscall_context *context,
				 uint64_t epfd, uint64_t events, uint64_t maxevents,
				 uint64_t timeout)
{
	int64_t ready;

	early_io_note_clock(context);
	ready = early_rootfs_epoll_pwait((int)epfd, events, (int)maxevents,
					 (int)timeout);
	if (ready != 0 || (int)timeout <= 0)
		return ready;
	early_io_sleep_ms(context, (int)timeout);
	early_io_note_clock(context);
	return early_rootfs_epoll_pwait((int)epfd, events, (int)maxevents,
					(int)timeout);
}

static int64_t parse_scm_rights(const struct early_user_services *user,
				uint64_t control, uint64_t controllen,
				int *fds, int *nfds, int32_t *cred, int *has_cred)
{
	uint8_t raw[CMSG_CAP];
	unsigned off = 0;
	int got = 0;

	*nfds = 0;
	if (has_cred)
		*has_cred = 0;
	if (!control || !controllen)
		return 0;
	if (controllen > sizeof(raw))
		return -EINVAL;
	if (!user || !user->copy_from_user ||
	    user->copy_from_user(raw, control, controllen) != 0)
		return -EFAULT;
	while (off + CMSG_HDR <= controllen)
	{
		uint64_t clen = ir0_early_le64(raw + off);
		int32_t level;
		int32_t type;
		unsigned data;
		unsigned nfd;
		unsigned i;

		if (clen < CMSG_HDR || off + clen > controllen)
			return -EINVAL;
		level = (int32_t)ir0_early_le32(raw + off + 8);
		type = (int32_t)ir0_early_le32(raw + off + 12);
		if (level != IR0_EARLY_SOL_SOCKET)
			return -EOPNOTSUPP;
		if (type == IR0_EARLY_SCM_CREDENTIALS)
		{
			if (clen < CMSG_HDR + IR0_EARLY_UCRED_BYTES)
				return -EINVAL;
			if (cred && has_cred)
			{
				cred[0] = (int32_t)ir0_early_le32(raw + off +
								  CMSG_HDR);
				cred[1] = (int32_t)ir0_early_le32(raw + off +
								  CMSG_HDR + 4);
				cred[2] = (int32_t)ir0_early_le32(raw + off +
								  CMSG_HDR + 8);
				*has_cred = 1;
			}
			off += ir0_early_cmsg_align((unsigned)clen);
			continue;
		}
		if (type != IR0_EARLY_SCM_RIGHTS)
			return -EOPNOTSUPP;
		data = (unsigned)(clen - CMSG_HDR);
		if (data % 4U)
			return -EINVAL;
		nfd = data / 4U;
		if (got + (int)nfd > IR0_EARLY_RIGHTS_MAX)
			return -EINVAL;
		for (i = 0; i < nfd; i++)
			fds[got + (int)i] =
				(int)ir0_early_le32(raw + off + CMSG_HDR + i * 4U);
		got += (int)nfd;
		off += ir0_early_cmsg_align((unsigned)clen);
	}
	*nfds = got;
	return 0;
}

static int64_t early_sendmsg(struct early_syscall_context *context,
			     uint64_t fd, uint64_t msg)
{
	const struct early_user_services *user;
	uint8_t hdr[IR0_EARLY_MSGHDR_BYTES];
	uint64_t name;
	uint64_t namelen;
	uint64_t iov;
	uint64_t iovlen;
	uint64_t control;
	uint64_t controllen;
	int rights[IR0_EARLY_RIGHTS_MAX];
	int nrights = 0;
	int32_t cred[3];
	int has_cred = 0;
	uint64_t i;
	int64_t total = 0;
	int64_t parsed;

	if (!context || !context->services ||
	    !(user = context->services->user) || !user->copy_from_user)
		return -ENOSYS;
	if (user->copy_from_user(hdr, msg, sizeof(hdr)) != 0)
		return -EFAULT;
	name = ir0_early_le64(hdr);
	namelen = ir0_early_le32(hdr + 8);
	iov = ir0_early_le64(hdr + 16);
	iovlen = ir0_early_le64(hdr + 24);
	control = ir0_early_le64(hdr + 32);
	controllen = ir0_early_le64(hdr + 40);
	if (iovlen > 8)
		return -EINVAL;
	parsed = parse_scm_rights(user, control, controllen, rights, &nrights,
				  cred, &has_cred);
	if (parsed < 0)
		return parsed;
	if (has_cred)
	{
		int64_t queued = early_rootfs_send_creds((int)fd, cred[0],
							 cred[1], cred[2]);

		if (queued < 0)
			return queued;
	}
	if (nrights)
	{
		int64_t queued = early_rootfs_send_rights((int)fd, rights, nrights);

		if (queued < 0)
			return queued;
	}
	for (i = 0; i < iovlen; i++)
	{
		uint64_t vec[2];
		int64_t n;

		if (user->copy_from_user(vec, iov + i * 16UL, sizeof(vec)) != 0)
			return total ? total : -EFAULT;
		if (vec[1] == 0)
			continue;
		n = early_rootfs_sendto((int)fd, vec[0], vec[1],
					namelen ? name : 0, namelen);
		if (n < 0)
			return total ? total : n;
		total += n;
		if ((uint64_t)n < vec[1])
			break;
	}
	return total;
}

static int64_t early_recvmsg(struct early_syscall_context *context,
			     uint64_t fd, uint64_t msg)
{
	const struct early_user_services *user;
	uint8_t hdr[IR0_EARLY_MSGHDR_BYTES];
	uint64_t iov;
	uint64_t iovlen;
	uint64_t control;
	uint64_t controllen;
	int rights[IR0_EARLY_RIGHTS_MAX];
	int nrights;
	uint64_t i;
	int64_t total = 0;
	uint32_t flags = 0;

	if (!context || !context->services ||
	    !(user = context->services->user) || !user->copy_from_user ||
	    !user->copy_to_user)
		return -ENOSYS;
	if (user->copy_from_user(hdr, msg, sizeof(hdr)) != 0)
		return -EFAULT;
	iov = ir0_early_le64(hdr + 16);
	iovlen = ir0_early_le64(hdr + 24);
	control = ir0_early_le64(hdr + 32);
	controllen = ir0_early_le64(hdr + 40);
	if (iovlen > 8)
		return -EINVAL;
	for (i = 0; i < iovlen; i++)
	{
		uint64_t vec[2];
		int64_t n;

		if (user->copy_from_user(vec, iov + i * 16UL, sizeof(vec)) != 0)
			return total ? total : -EFAULT;
		if (vec[1] == 0)
			continue;
		n = early_rootfs_recvfrom((int)fd, vec[0], vec[1]);
		if (n < 0)
			return total ? total : n;
		total += n;
		if ((uint64_t)n < vec[1])
			break;
	}
	nrights = (int)early_rootfs_recv_rights((int)fd, rights,
						IR0_EARLY_RIGHTS_MAX);
	if (nrights < 0)
		return total ? total : nrights;
	{
		int32_t cred[3];
		int has_cred = early_rootfs_take_creds((int)fd, &cred[0],
						       &cred[1], &cred[2]);
		unsigned need = 0;
		uint8_t cmsg[CMSG_CAP];
		unsigned k;

		if (nrights > 0)
			need += ir0_early_cmsg_align(CMSG_HDR +
						     (unsigned)nrights * 4U);
		if (has_cred)
			need += ir0_early_cmsg_align(CMSG_HDR +
						     IR0_EARLY_UCRED_BYTES);
		if (need && (!control || controllen < need))
		{
			int drop;

			for (drop = 0; drop < nrights; drop++)
				(void)early_rootfs_close(rights[drop]);
			flags |= IR0_EARLY_MSG_CTRUNC;
			need = 0;
		}
		else if (need)
		{
			unsigned off = 0;

			for (k = 0; k < sizeof(cmsg); k++)
				cmsg[k] = 0;
			if (nrights > 0)
			{
				unsigned clen = CMSG_HDR + (unsigned)nrights * 4U;

				ir0_early_put_le64(cmsg + off, clen);
				ir0_early_put_le32(cmsg + off + 8,
						   IR0_EARLY_SOL_SOCKET);
				ir0_early_put_le32(cmsg + off + 12,
						   IR0_EARLY_SCM_RIGHTS);
				for (k = 0; k < (unsigned)nrights; k++)
					ir0_early_put_le32(cmsg + off + CMSG_HDR +
							   k * 4U,
							   (uint32_t)rights[k]);
				off += ir0_early_cmsg_align(clen);
				if (context->services->time &&
				    context->services->time->note)
					context->services->time->note(
						EARLY_SYSCALL_SCM_RIGHTS_OK);
			}
			if (has_cred)
			{
				unsigned clen = CMSG_HDR + IR0_EARLY_UCRED_BYTES;

				ir0_early_put_le64(cmsg + off, clen);
				ir0_early_put_le32(cmsg + off + 8,
						   IR0_EARLY_SOL_SOCKET);
				ir0_early_put_le32(cmsg + off + 12,
						   IR0_EARLY_SCM_CREDENTIALS);
				ir0_early_put_le32(cmsg + off + CMSG_HDR,
						   (uint32_t)cred[0]);
				ir0_early_put_le32(cmsg + off + CMSG_HDR + 4,
						   (uint32_t)cred[1]);
				ir0_early_put_le32(cmsg + off + CMSG_HDR + 8,
						   (uint32_t)cred[2]);
			}
			if (user->copy_to_user(control, cmsg, need) != 0)
				return -EFAULT;
		}
		if (control)
		{
			ir0_early_put_le64(hdr + 40, need);
			if (user->copy_to_user(msg + 40, hdr + 40, 8) != 0)
				return -EFAULT;
		}
	}
	if (flags)
	{
		ir0_early_put_le32(hdr + 48, flags);
		if (user->copy_to_user(msg + 48, hdr + 48, 4) != 0)
			return -EFAULT;
	}
	return total;
}

static int64_t early_sendmmsg(struct early_syscall_context *context,
			      uint64_t fd, uint64_t msgvec, uint64_t vlen)
{
	const struct early_user_services *user;
	uint64_t i;
	uint32_t wrote;

	if (!vlen)
		return 0;
	if (!context || !context->services ||
	    !(user = context->services->user) || !user->copy_to_user)
		return -ENOSYS;
	if (vlen > 8)
		vlen = 8;
	for (i = 0; i < vlen; i++)
	{
		int64_t n = early_sendmsg(context, fd,
					  msgvec + i * IR0_EARLY_MMSGHDR_BYTES);

		if (n < 0)
			return i ? (int64_t)i : n;
		wrote = (uint32_t)n;
		if (user->copy_to_user(msgvec + i * IR0_EARLY_MMSGHDR_BYTES + 56,
				       &wrote, 4) != 0)
			return i ? (int64_t)i : -EFAULT;
	}
	return (int64_t)vlen;
}

static int64_t early_recvmmsg(struct early_syscall_context *context,
			      uint64_t fd, uint64_t msgvec, uint64_t vlen,
			      uint64_t timeout)
{
	const struct early_user_services *user;
	uint64_t i;
	uint32_t got;

	if (!vlen)
		return 0;
	if (!context || !context->services ||
	    !(user = context->services->user) || !user->copy_to_user)
		return -ENOSYS;
	if (vlen > 8)
		vlen = 8;
	for (i = 0; i < vlen; i++)
	{
		int64_t n = early_recvmsg(context, fd,
					  msgvec + i * IR0_EARLY_MMSGHDR_BYTES);

		if (n < 0)
		{
			if (i)
				return (int64_t)i;
			if (n == -EAGAIN && timeout)
				return 0;
			return n;
		}
		got = (uint32_t)n;
		if (user->copy_to_user(msgvec + i * IR0_EARLY_MMSGHDR_BYTES + 56,
				       &got, 4) != 0)
			return i ? (int64_t)i : -EFAULT;
	}
	return (int64_t)vlen;
}

static int64_t early_io_syscall(void *opaque, enum ir0_syscall_id id,
				uint64_t a0, uint64_t a1, uint64_t a2,
				uint64_t a3, uint64_t a4, uint64_t a5)
{
	early_io_note_clock(opaque);
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
	case IR0_SYSCALL_EVENTFD2:
		return early_rootfs_eventfd2(a0, (int)a1);
	case IR0_SYSCALL_MEMFD_CREATE:
		return early_rootfs_memfd_create(a0, (unsigned)a1);
	case IR0_SYSCALL_TIMERFD_CREATE:
		return early_rootfs_timerfd_create((int)a0, (int)a1);
	case IR0_SYSCALL_TIMERFD_SETTIME:
		return early_rootfs_timerfd_settime((int)a0, (int)a1, a2, a3);
	case IR0_SYSCALL_TIMERFD_GETTIME:
		return early_rootfs_timerfd_gettime((int)a0, a1);
	case IR0_SYSCALL_EPOLL_CREATE1:
		return early_rootfs_epoll_create1((int)a0);
	case IR0_SYSCALL_EPOLL_CTL:
		return early_rootfs_epoll_ctl((int)a0, (int)a1, (int)a2, a3);
	case IR0_SYSCALL_EPOLL_PWAIT:
		(void)a4;
		return early_epoll_pwait(opaque, a0, a1, a2, a3);
	case IR0_SYSCALL_EPOLL_PWAIT2:
	{
		int timeout = -1;
		const struct early_user_services *user;
		struct early_syscall_context *context = opaque;

		(void)a4;
		if (a3)
		{
			int64_t ts[2];

			user = context && context->services ? context->services->user : 0;
			if (!user || !user->copy_from_user ||
			    user->copy_from_user(ts, a3, sizeof(ts)) != 0)
				return -EFAULT;
			if (ts[0] < 0 || ts[1] < 0 || ts[1] >= 1000000000LL)
				return -EINVAL;
			if (ts[0] == 0 && ts[1] == 0)
				timeout = 0;
			else if (ts[0] > 2147483)
				timeout = 2147483647;
			else
				timeout = (int)(ts[0] * 1000 + ts[1] / 1000000);
		}
		return early_epoll_pwait(opaque, a0, a1, a2, (uint64_t)timeout);
	}
	case IR0_SYSCALL_SOCKET:
		return early_rootfs_socket((int)a0, (int)a1, (int)a2);
	case IR0_SYSCALL_SOCKETPAIR:
		return early_rootfs_socketpair((int)a0, (int)a1, (int)a2, a3);
	case IR0_SYSCALL_BIND:
		return early_rootfs_sock_bind((int)a0, a1, a2);
	case IR0_SYSCALL_LISTEN:
		return early_rootfs_sock_listen((int)a0, (int)a1);
	case IR0_SYSCALL_CONNECT:
		return early_rootfs_sock_connect((int)a0, a1, a2);
	case IR0_SYSCALL_ACCEPT:
		return early_rootfs_sock_accept4((int)a0, a1, a2, 0);
	case IR0_SYSCALL_ACCEPT4:
		return early_rootfs_sock_accept4((int)a0, a1, a2, (int)a3);
	case IR0_SYSCALL_GETSOCKNAME:
		return early_rootfs_getsockname((int)a0, a1, a2);
	case IR0_SYSCALL_GETPEERNAME:
		return early_rootfs_getpeername((int)a0, a1, a2);
	case IR0_SYSCALL_SENDTO:
	{
		int64_t ret = early_rootfs_sendto((int)a0, a1, a2, a4, a5);
		struct early_syscall_context *context = opaque;

		if (ret >= 0 && early_rootfs_abstract_observed() && context &&
		    context->services && context->services->time &&
		    context->services->time->note)
			context->services->time->note(EARLY_SYSCALL_ABSTRACT_OK);
		return ret;
	}
	case IR0_SYSCALL_RECVFROM:
		return early_rootfs_recvfrom((int)a0, a1, a2);
	case IR0_SYSCALL_SENDMSG:
		return early_sendmsg(opaque, a0, a1);
	case IR0_SYSCALL_RECVMSG:
		return early_recvmsg(opaque, a0, a1);
	case IR0_SYSCALL_SHUTDOWN:
		return early_rootfs_shutdown((int)a0, (int)a1);
	case IR0_SYSCALL_SETSOCKOPT:
		return early_rootfs_setsockopt((int)a0, (int)a1, (int)a2, a3, a4);
	case IR0_SYSCALL_GETSOCKOPT:
		return early_rootfs_getsockopt((int)a0, (int)a1, (int)a2, a3, a4);
	case IR0_SYSCALL_PPOLL:
		(void)a3;
		(void)a4;
		return early_ppoll(opaque, a0, a1, a2);
	case IR0_SYSCALL_PSELECT6:
		return early_pselect6(opaque, a0, a1, a2, a3, a4);
	case IR0_SYSCALL_PREAD64:
		return early_rootfs_pread64((int)a0, a1, a2, (int64_t)a3);
	case IR0_SYSCALL_PWRITE64:
		return early_rootfs_pwrite64((int)a0, a1, a2, (int64_t)a3);
	case IR0_SYSCALL_SENDFILE:
		return early_rootfs_sendfile((int)a0, (int)a1, a2, a3);
	case IR0_SYSCALL_PREADV:
		return early_preadv(opaque, a0, a1, a2, (int64_t)a3);
	case IR0_SYSCALL_PWRITEV:
		return early_pwritev(opaque, a0, a1, a2, (int64_t)a3);
	case IR0_SYSCALL_SPLICE:
	{
		int64_t ret = early_rootfs_splice((int)a0, a1, (int)a2, a3, a4,
						  (unsigned)a5);
		struct early_syscall_context *context = opaque;

		if (ret > 0 && context && context->services &&
		    context->services->time && context->services->time->note)
			context->services->time->note(EARLY_SYSCALL_SPLICE_OK);
		return ret;
	}
	case IR0_SYSCALL_TEE:
		return early_rootfs_tee((int)a0, (int)a1, a2, (unsigned)a3);
	case IR0_SYSCALL_CLOSE_RANGE:
		return early_rootfs_close_range((unsigned)a0, (unsigned)a1,
						(unsigned)a2);
	case IR0_SYSCALL_COPY_FILE_RANGE:
		return early_rootfs_copy_file_range((int)a0, a1, (int)a2, a3, a4,
						    (unsigned)a5);
	case IR0_SYSCALL_FADVISE64:
		return early_rootfs_fadvise64((int)a0, (int64_t)a1, a2, (int)a3);
	case IR0_SYSCALL_VMSPLICE:
		return early_rootfs_vmsplice((int)a0, a1, a2, (unsigned)a3);
	case IR0_SYSCALL_SYNC_FILE_RANGE:
		return early_rootfs_sync_file_range((int)a0, (int64_t)a1,
						    (int64_t)a2, (unsigned)a3);
	case IR0_SYSCALL_SENDMMSG:
		return early_sendmmsg(opaque, a0, a1, a2);
	case IR0_SYSCALL_RECVMMSG:
		(void)a3;
		return early_recvmmsg(opaque, a0, a1, a2, a4);
	case IR0_SYSCALL_READAHEAD:
		return early_rootfs_readahead((int)a0, (int64_t)a1, a2);
	case IR0_SYSCALL_SYNCFS:
		return early_rootfs_syncfs((int)a0);
	case IR0_SYSCALL_PREADV2:
		if (a5 & ~(uint64_t)IR0_EARLY_RWF_KNOWN)
			return a5 & IR0_EARLY_RWF_APPEND ? -EOPNOTSUPP : -EINVAL;
		return early_preadv(opaque, a0, a1, a2,
				    (int64_t)(((uint64_t)a4 << 32) | a3));
	case IR0_SYSCALL_PWRITEV2:
		if (a5 & ~(uint64_t)IR0_EARLY_RWF_KNOWN)
			return a5 & IR0_EARLY_RWF_APPEND ? -EOPNOTSUPP : -EINVAL;
		return early_pwritev(opaque, a0, a1, a2,
				     (int64_t)(((uint64_t)a4 << 32) | a3));
	default:
		return -ENOSYS;
	}
}

static const enum ir0_syscall_id g_io_syscalls[] = {
	IR0_SYSCALL_WRITE, IR0_SYSCALL_WRITEV, IR0_SYSCALL_READV,
	IR0_SYSCALL_IOCTL, IR0_SYSCALL_FCNTL,
	IR0_SYSCALL_DUP, IR0_SYSCALL_DUP3, IR0_SYSCALL_PPOLL,
	IR0_SYSCALL_PSELECT6, IR0_SYSCALL_EVENTFD2, IR0_SYSCALL_MEMFD_CREATE,
	IR0_SYSCALL_TIMERFD_CREATE, IR0_SYSCALL_TIMERFD_SETTIME,
	IR0_SYSCALL_TIMERFD_GETTIME, IR0_SYSCALL_EPOLL_CREATE1,
	IR0_SYSCALL_EPOLL_CTL, IR0_SYSCALL_EPOLL_PWAIT, IR0_SYSCALL_EPOLL_PWAIT2,
	IR0_SYSCALL_SOCKET,
	IR0_SYSCALL_BIND, IR0_SYSCALL_LISTEN,
	IR0_SYSCALL_CONNECT, IR0_SYSCALL_ACCEPT, IR0_SYSCALL_ACCEPT4,
	IR0_SYSCALL_GETSOCKNAME, IR0_SYSCALL_GETPEERNAME, IR0_SYSCALL_SENDTO,
	IR0_SYSCALL_RECVFROM, IR0_SYSCALL_SENDMSG, IR0_SYSCALL_RECVMSG,
	IR0_SYSCALL_SHUTDOWN, IR0_SYSCALL_SETSOCKOPT, IR0_SYSCALL_GETSOCKOPT,
	IR0_SYSCALL_PREAD64, IR0_SYSCALL_PWRITE64, IR0_SYSCALL_SENDFILE,
	IR0_SYSCALL_PREADV, IR0_SYSCALL_PWRITEV, IR0_SYSCALL_SPLICE,
	IR0_SYSCALL_TEE, IR0_SYSCALL_CLOSE_RANGE, IR0_SYSCALL_COPY_FILE_RANGE,
	IR0_SYSCALL_FADVISE64, IR0_SYSCALL_VMSPLICE, IR0_SYSCALL_SYNC_FILE_RANGE,
	IR0_SYSCALL_SENDMMSG, IR0_SYSCALL_RECVMMSG, IR0_SYSCALL_READAHEAD,
	IR0_SYSCALL_SYNCFS, IR0_SYSCALL_PREADV2, IR0_SYSCALL_PWRITEV2,
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
