/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 console / TTY facade implementation.
 */

#include <ir0/console.h>
#include <ir0/console_backend.h>
#include <ir0/errno.h>
#include <ir0/keyboard.h>
#include <kernel/process.h>
#include <sched/scheduler_api.h>
#include <string.h>

#define IR0_CONSOLE_MAX_READ_WAITERS 8
#define IR0_CONSOLE_ECHO_COLOR       0x07u
#define IR0_CONSOLE_ECHO_PENDING     64

static process_t *console_read_waiters[IR0_CONSOLE_MAX_READ_WAITERS];
static struct ir0_termios console_termios;
static int console_termios_ready;
static int console_userspace_attached;
static int console_input_echo_force;

static char echo_pending[IR0_CONSOLE_ECHO_PENDING];
static volatile int echo_pending_head;
static volatile int echo_pending_tail;
static int console_resched_pending;

static void ir0_console_echo_flush_cursor(void);

static void console_termios_ensure(void)
{
	if (!console_termios_ready)
	{
		memset(&console_termios, 0, sizeof(console_termios));
		console_termios.c_iflag = IR0_CONSOLE_IFLAG_DEFAULT;
		console_termios.c_oflag = IR0_CONSOLE_OFLAG_DEFAULT;
		console_termios.c_cflag = IR0_CONSOLE_CFLAG_DEFAULT;
		console_termios.c_lflag = IR0_CONSOLE_LFLAG_DEFAULT;
		console_termios.c_line = 0;
		console_termios.c_cc[4] = 4;  /* VEOF ^D */
		console_termios.c_cc[5] = 0;  /* VTIME */
		console_termios.c_cc[6] = 1;  /* VMIN */
		console_termios.c_ispeed = 38400;
		console_termios.c_ospeed = 38400;
		console_termios_ready = 1;
	}
}

static int ir0_console_block_current(void)
{
	int i;

	if (!current_process)
		return 0;

	for (i = 0; i < IR0_CONSOLE_MAX_READ_WAITERS; i++)
	{
		if (console_read_waiters[i] == NULL)
		{
			console_read_waiters[i] = current_process;
			current_process->state = PROCESS_BLOCKED;
			ir0_console_echo_flush_cursor();
			sched_schedule_next();
			return 1;
		}
	}

	current_process->state = PROCESS_BLOCKED;
	sched_schedule_next();
	return 1;
}

void ir0_console_wake_readers(void)
{
	int i;
	int woke = 0;

	if (!ir0_console_poll())
		return;

	for (i = 0; i < IR0_CONSOLE_MAX_READ_WAITERS; i++)
	{
		if (console_read_waiters[i])
		{
			process_t *reader = console_read_waiters[i];

			if (reader->state == PROCESS_ZOMBIE)
			{
				console_read_waiters[i] = NULL;
				continue;
			}
			reader->state = PROCESS_READY;
			console_read_waiters[i] = NULL;
			woke = 1;
		}
	}

	if (woke)
		console_resched_pending = 1;
}

int ir0_console_take_resched(void)
{
	int pending = console_resched_pending;

	console_resched_pending = 0;
	return pending;
}

int ir0_console_poll(void)
{
	return keyboard_buffer_has_data() ? 1 : 0;
}

int ir0_console_has_blocked_reader(void)
{
	int i;

	for (i = 0; i < IR0_CONSOLE_MAX_READ_WAITERS; i++)
	{
		if (console_read_waiters[i])
			return 1;
	}
	return 0;
}

void ir0_console_purge_waiters_for_process(process_t *p)
{
	int i;

	if (!p)
		return;

	for (i = 0; i < IR0_CONSOLE_MAX_READ_WAITERS; i++)
	{
		if (console_read_waiters[i] == p)
			console_read_waiters[i] = NULL;
	}
}

static void ir0_console_echo_char(char c)
{
	char echo_buf[4];
	size_t echo_len = 0;

	console_termios_ensure();

	if (!(console_termios.c_lflag & IR0_LFLAG_ECHO) && !console_input_echo_force)
		return;

	if (c == '\b' || c == 127)
	{
		echo_buf[echo_len++] = '\b';
		echo_buf[echo_len++] = ' ';
		echo_buf[echo_len++] = '\b';
	}
	else if (c == '\r')
	{
		if (console_termios.c_iflag & IR0_IFLAG_ICRNL)
			echo_buf[echo_len++] = '\n';
		else
			echo_buf[echo_len++] = '\r';
	}
	else if (c == '\n')
	{
		echo_buf[echo_len++] = '\n';
	}
	else if (c == '\t' || c >= ' ')
	{
		echo_buf[echo_len++] = c;
	}

	if (echo_len > 0)
		console_backend_write(echo_buf, echo_len, IR0_CONSOLE_ECHO_COLOR);
}

static void ir0_console_echo_flush_cursor(void)
{
	console_backend_show_cursor(IR0_CONSOLE_ECHO_COLOR);
}

void ir0_console_input_enqueue(char c)
{
	int next = (echo_pending_head + 1) % IR0_CONSOLE_ECHO_PENDING;

	if (next == echo_pending_tail)
		return;

	echo_pending[echo_pending_head] = c;
	echo_pending_head = next;
}

void ir0_console_drain_echo(void)
{
	int did_echo = 0;

	while (echo_pending_tail != echo_pending_head)
	{
		char c = echo_pending[echo_pending_tail];

		echo_pending_tail = (echo_pending_tail + 1) % IR0_CONSOLE_ECHO_PENDING;
		ir0_console_echo_char(c);
		did_echo = 1;
	}
	if (did_echo || ir0_console_has_blocked_reader())
		ir0_console_echo_flush_cursor();
}

void ir0_console_on_userspace_attach(void)
{
	if (console_userspace_attached)
		return;
	console_userspace_attached = 1;
	console_input_echo_force = 1;
	console_backend_userspace_handoff();
}

int64_t ir0_console_read(void *kbuf, size_t count, int nonblock)
{
	size_t bytes_read = 0;
	char *buf = (char *)kbuf;

	if (!kbuf)
		return -EFAULT;
	if (count == 0)
		return 0;

	console_termios_ensure();

	for (;;)
	{
		while (bytes_read < count && keyboard_buffer_has_data())
		{
			char c = keyboard_buffer_get();

			if (c == '\r' && (console_termios.c_iflag & IR0_IFLAG_ICRNL))
				c = '\n';

			if (c != 0)
				buf[bytes_read++] = c;
		}

		if (bytes_read > 0)
			return (int64_t)bytes_read;

		if (nonblock)
			return -EAGAIN;

		(void)ir0_console_block_current();
	}
}

int64_t ir0_console_write(const void *kbuf, size_t count, uint8_t color)
{
	const char *buf = (const char *)kbuf;
	size_t i;

	if (!kbuf)
		return -EFAULT;

	console_termios_ensure();

	for (i = 0; i < count; i++)
	{
		char c = buf[i];

		if (c == '\n' &&
		    (console_termios.c_oflag & (IR0_OFLAG_OPOST | IR0_OFLAG_ONLCR)) ==
		    (IR0_OFLAG_OPOST | IR0_OFLAG_ONLCR))
		{
			char cr = '\r';

			console_backend_write(&cr, 1, color);
		}
		console_backend_write(&c, 1, color);
	}

	return (int64_t)count;
}

int ir0_console_isatty(void)
{
	return 1;
}

int ir0_console_fill_termios(struct ir0_termios *out)
{
	if (!out)
		return -EINVAL;

	console_termios_ensure();
	*out = console_termios;
	return 0;
}

int ir0_console_set_termios(const struct ir0_termios *in)
{
	if (!in)
		return -EINVAL;

	console_termios = *in;
	console_termios_ready = 1;
	if (console_userspace_attached)
		console_input_echo_force = 1;
	return 0;
}
