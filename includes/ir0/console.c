/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: console.c
 * Description: IR0 kernel source — console
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/copy_user.h>
#include <ir0/console.h>
#include <ir0/paging.h>
#include <ir0/arch_port.h>
#include <ir0/console_backend.h>
#include <ir0/ash_smoke.h>
#include <d1_12_read_diag.h>
#include <d1_16_tty_read_diag.h>
#include <ir0/errno.h>
#include <ir0/input_backend.h>
#include <ir0/video_console.h>
#include <ir0/kernel.h>
#include <ir0/process.h>
#include <ir0/sched.h>
#include <string.h>

extern void kernel_idle_poll(void);

#define IR0_TTY_MAX_READ_WAITERS 8
#define IR0_TTY_ECHO_COLOR       0x07u
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
		d1_12_read_diag_tty_line(canon_readq_len - 1, canon_readq,
					 canon_readq_len);
		d1_16_tty_line_ready((uintptr_t)(void *)&canon_readq,
				     canon_readq_len);
		return 1;
	}

	if (canon_line_len + 1 >= IR0_TTY_CANON_MAX)
		return 0;

	canon_line[canon_line_len++] = c;
	if (tty_echo_on())
		tty_echo_char(c);
	return 0;
}

static void tty_wake_stage_user_read(process_t *proc)
{
	char kbuf[IR0_TTY_CANON_MAX + 1];
	size_t out = 0;
	uintptr_t user_buf;
	size_t req;
	int64_t n;

	if (!proc || proc->mode != USER_MODE || !proc->irq_frame_saved)
		return;
	if (canon_readq_pos >= canon_readq_len)
		return;

	while (out < sizeof(kbuf) && canon_readq_pos < canon_readq_len)
		tty_canon_drain(kbuf, sizeof(kbuf), &out);
	if (out == 0)
		return;

	user_buf = (uintptr_t)proc->syscall_frame.rsi;
	req = proc->syscall_frame.rdx;
	if (req > 0 && out > req)
		out = req;
	if (!proc->page_directory || user_buf == 0 || out == 0)
		return;

	if (copy_to_user_region_in_directory(proc->page_directory, user_buf,
					     kbuf, out) != 0)
	{
		proc->syscall_resume_rax = (uint64_t)(-EFAULT);
		return;
	}

	n = (int64_t)out;
	proc->syscall_resume_rax = (uint64_t)n;
	d1_12_read_diag_kcopy(n, req, kbuf, out);
	ir0_ash_smoke_read_return(0, n);
}

static inline uint64_t tty_irq_save(void)
{
	return (uint64_t)arch_irq_save();
}

static inline void tty_irq_restore(uint64_t flags)
{
	arch_irq_restore((unsigned long)flags);
}

static int tty_waiter_count(void)
{
	int i;
	int n = 0;

	for (i = 0; i < IR0_TTY_MAX_READ_WAITERS; i++)
	{
		if (tty_read_waiters[i])
			n++;
	}
	return n;
}

static void tty_waiter_remove(process_t *p)
{
	int i;

	for (i = 0; i < IR0_TTY_MAX_READ_WAITERS; i++)
	{
		if (tty_read_waiters[i] == p)
			tty_read_waiters[i] = NULL;
	}
}

static int tty_waiter_register(process_t *p)
{
	int i;

	for (i = 0; i < IR0_TTY_MAX_READ_WAITERS; i++)
	{
		if (tty_read_waiters[i] == p)
			return 1;
	}

	for (i = 0; i < IR0_TTY_MAX_READ_WAITERS; i++)
	{
		if (tty_read_waiters[i] == NULL)
		{
			tty_read_waiters[i] = p;
			return 1;
		}
	}

	return 0;
}

/*
 * Linux-like prepare_to_wait: register on the TTY wait queue, re-check the
 * canonical line under IRQ mask, then block.  Without the re-check, a keyboard
 * IRQ can complete the line and wake the reader between registration and
 * PROCESS_BLOCKED, leaving the task blocked with no waiter and data ready.
 */
static int tty_sleep_for_input(void)
{
	process_t *proc = current_process;
	uint64_t flags;
	int blocked_once = 0;
	int prev_state;

	if (!proc)
		return 0;

	if (!tty_waiter_register(proc))
		return 0;

	d1_16_tty_read_block(proc, tty_waiter_count(), "tty_input");

	for (;;)
	{
		if (ir0_console_input_ready())
		{
			tty_waiter_remove(proc);
			process_clear_in_thread_syscall_block(proc);
			d1_16_tty_read_resume(proc, "input_ready");
			return 1;
		}

		flags = tty_irq_save();
		if (ir0_console_input_ready())
		{
			tty_irq_restore(flags);
			tty_waiter_remove(proc);
			process_clear_in_thread_syscall_block(proc);
			d1_16_tty_read_resume(proc, "input_ready_irq");
			return 1;
		}

		prev_state = proc->state;
		if (proc->mode == USER_MODE)
			process_arm_blocked_syscall_resume(proc, 0);
		if (proc->state != PROCESS_READY)
		{
			proc->state = PROCESS_BLOCKED;
			blocked_once = 1;
			d1_16_tty_state_transition(proc, prev_state,
						   PROCESS_BLOCKED);
		}
		tty_irq_restore(flags);

		sched_schedule_next();

		if (proc->state != PROCESS_BLOCKED)
		{
			if (blocked_once)
				d1_16_tty_state_transition(proc, PROCESS_BLOCKED,
							   proc->state);
			tty_waiter_remove(proc);
			d1_16_tty_read_resume(proc, "woke");
			return 1;
		}

		/*
		 * All tasks blocked: RR has no idle thread yet, so poll PS/2 and
		 * wake TTY waiters from syscall context (QEMU GTK often skips IRQ1).
		 */
		kernel_idle_poll();

		if (ir0_console_input_ready())
		{
			prev_state = proc->state;
			proc->state = PROCESS_READY;
			d1_16_tty_state_transition(proc, prev_state,
						   PROCESS_READY);
			tty_waiter_remove(proc);
			d1_16_tty_read_resume(proc, "poll_ready");
			return 1;
		}
	}
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
		if (line_done)
		{
			ir0_ash_smoke_tty_line_ready();
			if (ir0_console_wake_readers())
				sched_schedule_next();
		}
	}
	else if (tty_echo_on())
		tty_echo_char(nc);
}

int ir0_console_input_ready(void)
{
	if (canon_readq_pos < canon_readq_len)
		return 1;
	if (!tty_icanon_on() && input_kbd_has_data())
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

		while (bytes_read < count && input_kbd_has_data())
		{
			char c = input_kbd_get();

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
	input_kbd_clear();
	canon_line_len = 0;
	canon_readq_len = 0;
	canon_readq_pos = 0;
}

int ir0_console_wake_readers(void)
{
	int i;
	int woke = 0;
	int waiters_before = tty_waiter_count();
	uint32_t last_pid = 0;

	if (!ir0_console_input_ready())
		return 0;

	for (i = 0; i < IR0_TTY_MAX_READ_WAITERS; i++)
	{
		if (tty_read_waiters[i])
		{
			process_t *reader = tty_read_waiters[i];
			int prev_state;

			if (reader->state == PROCESS_ZOMBIE)
			{
				tty_read_waiters[i] = NULL;
				continue;
			}
			if (reader->mode == USER_MODE && reader->irq_frame_saved)
				tty_wake_stage_user_read(reader);
			prev_state = reader->state;
			reader->state = PROCESS_READY;
			d1_16_tty_state_transition(reader, prev_state,
						   PROCESS_READY);
			last_pid = (uint32_t)reader->task.pid;
			tty_read_waiters[i] = NULL;
			woke = 1;
		}
	}

	if (woke)
		d1_16_tty_wake(waiters_before, woke, last_pid);

	return woke;
}

int ir0_console_take_resched(void)
{
	return 0;
}

int ir0_console_timer_resched_pending(void)
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
	d1_16_tty_read_pre(current_process, 0, nonblock,
			   (uintptr_t)(void *)&canon_readq);
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
	{
		int scale = console_backend_fb_scale();

		if (scale < 1)
			scale = 1;
		win.ws_xpixel = (uint16_t)(win.ws_col * 8u * (uint16_t)scale);
		win.ws_ypixel = (uint16_t)(win.ws_row * 16u * (uint16_t)scale);
	}
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
