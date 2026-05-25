/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 console / TTY — minimal line discipline (kernel buffers only).
 *
 * User copy in syscall/devfs; this module never touches user pointers.
 */

#include <ir0/copy_user.h>
#include <ir0/console.h>
#include <ir0/console_backend.h>
#include <ir0/errno.h>
#include <ir0/keyboard.h>
#include <drivers/video/console.h>
#include <kernel/kernel.h>
#include <kernel/process.h>
#include <sched/scheduler_api.h>
#include <string.h>

#define IR0_TTY_MAX_READ_WAITERS 8
#define IR0_TTY_ECHO_COLOR       0x0Fu
#define IR0_TTY_CANON_MAX        256

static process_t *tty_read_waiters[IR0_TTY_MAX_READ_WAITERS];
static struct ir0_termios tty_termios;
static int tty_termios_ready;
static int tty_userspace_attached;

static char canon_line[IR0_TTY_CANON_MAX];
static size_t canon_line_len;

static char canon_readq[IR0_TTY_CANON_MAX + 1];
static size_t canon_readq_len;
static size_t canon_readq_pos;

static void tty_termios_ensure(void)
{
	if (!tty_termios_ready)
	{
		memset(&tty_termios, 0, sizeof(tty_termios));
		tty_termios.c_iflag = IR0_CONSOLE_IFLAG_DEFAULT;
		tty_termios.c_oflag = IR0_CONSOLE_OFLAG_DEFAULT;
		tty_termios.c_cflag = IR0_CONSOLE_CFLAG_DEFAULT;
		tty_termios.c_lflag = IR0_CONSOLE_LFLAG_DEFAULT;
		tty_termios.c_line = 0;
		tty_termios.c_cc[IR0_CC_VEOF] = 4;
		tty_termios.c_cc[IR0_CC_VTIME] = 0;
		tty_termios.c_cc[IR0_CC_VMIN] = 1;
		tty_termios.c_cc[IR0_CC_VERASE] = 127;
		tty_termios.c_ispeed = 38400;
		tty_termios.c_ospeed = 38400;
		tty_termios_ready = 1;
	}
}

static int tty_echo_on(void)
{
	tty_termios_ensure();
	return (tty_termios.c_lflag & IR0_LFLAG_ECHO) ? 1 : 0;
}

static int tty_icanon_on(void)
{
	tty_termios_ensure();
	return (tty_termios.c_lflag & IR0_LFLAG_ICANON) ? 1 : 0;
}

static char tty_normalize_input(char c)
{
	tty_termios_ensure();
	if (c == '\r' && (tty_termios.c_iflag & IR0_IFLAG_ICRNL))
		return '\n';
	return c;
}

static void tty_echo_char(char c)
{
	char echo_buf[4];
	size_t echo_len = 0;

	if (!tty_echo_on())
		return;

	if (c == '\b' || c == 127)
	{
		echo_buf[echo_len++] = '\b';
		echo_buf[echo_len++] = ' ';
		echo_buf[echo_len++] = '\b';
	}
	else if (c == '\r')
	{
		if (tty_termios.c_iflag & IR0_IFLAG_ICRNL)
			echo_buf[echo_len++] = '\n';
		else
			echo_buf[echo_len++] = '\r';
	}
	else if (c == '\n' || c == '\t' || (unsigned char)c >= ' ')
	{
		echo_buf[echo_len++] = c;
	}

	if (echo_len > 0)
		console_backend_write(echo_buf, echo_len, IR0_TTY_ECHO_COLOR);
}

static void tty_canon_erase(void)
{
	if (!tty_echo_on())
		return;
	if (!(tty_termios.c_lflag & IR0_LFLAG_ECHOE))
		return;
	tty_echo_char('\b');
}

static void tty_canon_drain(char *kbuf, size_t count, size_t *out_len)
{
	size_t avail;
	size_t n;

	if (canon_readq_pos >= canon_readq_len)
	{
		canon_readq_len = 0;
		canon_readq_pos = 0;
		return;
	}

	avail = canon_readq_len - canon_readq_pos;
	n = avail;
	if (n > count - *out_len)
		n = count - *out_len;
	if (n == 0)
		return;

	memcpy(kbuf + *out_len, canon_readq + canon_readq_pos, n);
	canon_readq_pos += n;
	*out_len += n;

	if (canon_readq_pos >= canon_readq_len)
	{
		canon_readq_len = 0;
		canon_readq_pos = 0;
	}
}

/*
 * Returns 1 when a completed canonical line is queued in canon_readq.
 */
static int tty_canon_feed(char c)
{
	size_t i;
	unsigned char erase;

	tty_termios_ensure();
	erase = tty_termios.c_cc[IR0_CC_VERASE];
	if (erase == 0)
		erase = 127;

	c = tty_normalize_input(c);

	if (c == '\b' || c == 127 || (unsigned char)c == erase)
	{
		if (canon_line_len > 0)
		{
			canon_line_len--;
			tty_canon_erase();
		}
		return 0;
	}

	if (c == '\n')
	{
		if (tty_echo_on())
			tty_echo_char('\n');
		canon_readq_len = 0;
		canon_readq_pos = 0;
		for (i = 0; i < canon_line_len; i++)
			canon_readq[canon_readq_len++] = canon_line[i];
		canon_readq[canon_readq_len++] = '\n';
		canon_line_len = 0;
		return 1;
	}

	if (canon_line_len + 1 >= IR0_TTY_CANON_MAX)
		return 0;

	canon_line[canon_line_len++] = c;
	if (tty_echo_on())
		tty_echo_char(c);
	return 0;
}

static int tty_sleep_for_input(void)
{
	int i;

	if (!current_process)
		return 0;

	for (i = 0; i < IR0_TTY_MAX_READ_WAITERS; i++)
	{
		if (tty_read_waiters[i] == current_process)
			goto block;
	}

	for (i = 0; i < IR0_TTY_MAX_READ_WAITERS; i++)
	{
		if (tty_read_waiters[i] == NULL)
		{
			tty_read_waiters[i] = current_process;
			goto block;
		}
	}

	return 0;

block:
	current_process->state = PROCESS_BLOCKED;
	for (;;)
	{
		sched_schedule_next();
		if (current_process->state != PROCESS_BLOCKED)
			break;
		/*
		 * All tasks blocked: RR has no idle thread yet, so poll PS/2 and
		 * wake TTY waiters from syscall context (QEMU GTK often skips IRQ1).
		 */
		kernel_idle_poll();
	}
	return 1;
}

void tty_input_char(char c)
{
	ir0_console_keypress(c);
}

void ir0_console_keypress(char c)
{
	char nc;
	int line_done;

	tty_termios_ensure();
	nc = tty_normalize_input(c);
	if (nc == 0)
		return;

	if (tty_icanon_on())
	{
		line_done = tty_canon_feed(nc);
	}
	else if (tty_echo_on())
		tty_echo_char(nc);
}

int ir0_console_input_ready(void)
{
	if (canon_readq_pos < canon_readq_len)
		return 1;
	if (!tty_icanon_on() && keyboard_buffer_has_data())
		return 1;
	return 0;
}

int ir0_console_store_key_in_ring(void)
{
	tty_termios_ensure();
	return tty_icanon_on() ? 0 : 1;
}

int64_t tty_read_kernel(char *kbuf, size_t count, int nonblock)
{
	size_t bytes_read = 0;

	if (!kbuf)
		return -EFAULT;
	if (count == 0)
		return 0;

	tty_termios_ensure();

	for (;;)
	{
		while (bytes_read < count && canon_readq_pos < canon_readq_len)
			tty_canon_drain(kbuf, count, &bytes_read);

		if (bytes_read > 0)
			return (int64_t)bytes_read;

		if (tty_icanon_on())
		{
			if (nonblock)
				return -EAGAIN;
			(void)tty_sleep_for_input();
			continue;
		}

		while (bytes_read < count && keyboard_buffer_has_data())
		{
			char c = keyboard_buffer_get();

			c = tty_normalize_input(c);
			if (c == 0)
				continue;

			kbuf[bytes_read++] = c;
			if (tty_termios.c_cc[IR0_CC_VMIN] == 0)
				return (int64_t)bytes_read;
			if (bytes_read >= (size_t)tty_termios.c_cc[IR0_CC_VMIN])
				return (int64_t)bytes_read;
		}

		if (bytes_read > 0)
			return (int64_t)bytes_read;

		if (nonblock)
			return -EAGAIN;

		(void)tty_sleep_for_input();
	}
}

int64_t tty_write_kernel(const char *kbuf, size_t count, uint8_t color)
{
	size_t i;

	if (!kbuf)
		return -EFAULT;

	tty_termios_ensure();

	for (i = 0; i < count; i++)
	{
		char c = kbuf[i];

		if (c == '\n' &&
		    (tty_termios.c_oflag & (IR0_OFLAG_OPOST | IR0_OFLAG_ONLCR)) ==
		    (IR0_OFLAG_OPOST | IR0_OFLAG_ONLCR))
		{
			char cr = '\r';

			console_backend_write(&cr, 1, color);
		}
		console_backend_write(&c, 1, color);
	}

	return (int64_t)count;
}

int tty_ioctl_termios_kernel(uint64_t request, struct ir0_termios *ktermios)
{
	if (!ktermios)
		return -EINVAL;

	if (request == IR0_CONSOLE_TCGETS)
	{
		tty_termios_ensure();
		*ktermios = tty_termios;
		return 0;
	}

	if (request == IR0_CONSOLE_TCSETS ||
	    request == IR0_CONSOLE_TCSETSW ||
	    request == IR0_CONSOLE_TCSETSF)
	{
		tty_termios = *ktermios;
		tty_termios_ready = 1;
		canon_line_len = 0;
		canon_readq_len = 0;
		canon_readq_pos = 0;
		if (request == IR0_CONSOLE_TCSETSF)
			tty_flush_input();
		return 0;
	}

	return -ENOTTY;
}

void tty_flush_input(void)
{
	keyboard_buffer_clear();
	canon_line_len = 0;
	canon_readq_len = 0;
	canon_readq_pos = 0;
}

int ir0_console_wake_readers(void)
{
	int i;
	int woke = 0;

	if (!ir0_console_input_ready())
		return 0;

	for (i = 0; i < IR0_TTY_MAX_READ_WAITERS; i++)
	{
		if (tty_read_waiters[i])
		{
			process_t *reader = tty_read_waiters[i];

			if (reader->state == PROCESS_ZOMBIE)
			{
				tty_read_waiters[i] = NULL;
				continue;
			}
			reader->state = PROCESS_READY;
			tty_read_waiters[i] = NULL;
			woke = 1;
		}
	}

	return woke;
}

int ir0_console_take_resched(void)
{
	return 0;
}

int ir0_console_poll(void)
{
	return ir0_console_input_ready();
}

int ir0_console_has_blocked_reader(void)
{
	int i;

	for (i = 0; i < IR0_TTY_MAX_READ_WAITERS; i++)
	{
		if (tty_read_waiters[i])
			return 1;
	}
	return 0;
}

void ir0_console_purge_waiters_for_process(process_t *p)
{
	int i;

	if (!p)
		return;

	for (i = 0; i < IR0_TTY_MAX_READ_WAITERS; i++)
	{
		if (tty_read_waiters[i] == p)
			tty_read_waiters[i] = NULL;
	}
}

void ir0_console_input_enqueue(char c)
{
	tty_input_char(c);
}

void ir0_console_drain_echo(void)
{
	(void)0;
}

void ir0_console_on_userspace_attach(void)
{
	if (tty_userspace_attached)
		return;
	tty_userspace_attached = 1;
	console_backend_userspace_handoff();
}

int ir0_console_in_userspace(void)
{
	return tty_userspace_attached;
}

int64_t ir0_console_read(void *kbuf, size_t count, int nonblock)
{
	return tty_read_kernel((char *)kbuf, count, nonblock);
}

int64_t ir0_console_write(const void *kbuf, size_t count, uint8_t color)
{
	return tty_write_kernel((const char *)kbuf, count, color);
}

int ir0_console_isatty(void)
{
	return 1;
}

int ir0_console_term_width(void)
{
	int w = console_get_width();

	return w > 0 ? w : 80;
}

int ir0_console_term_height(void)
{
	int h = console_get_height();

	return h > 0 ? h : 25;
}

int ir0_console_ioctl_winsize(void *user_arg)
{
	struct ir0_winsize win;

	if (!user_arg)
		return -EINVAL;
	win.ws_row = (uint16_t)ir0_console_term_height();
	win.ws_col = (uint16_t)ir0_console_term_width();
	win.ws_xpixel = 0;
	win.ws_ypixel = 0;
	if (copy_to_user(user_arg, &win, sizeof(win)) != 0)
		return -EFAULT;
	return 0;
}

int ir0_console_fill_termios(struct ir0_termios *out)
{
	return tty_ioctl_termios_kernel(IR0_CONSOLE_TCGETS, out);
}

int ir0_console_set_termios(const struct ir0_termios *in)
{
	struct ir0_termios tmp;

	if (!in)
		return -EINVAL;
	tmp = *in;
	return tty_ioctl_termios_kernel(IR0_CONSOLE_TCSETSW, &tmp);
}

void ir0_console_flush_input(void)
{
	tty_flush_input();
}
