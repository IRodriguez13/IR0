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

#include <ir0/ktm/klog.h>
#include <ir0/copy_user.h>
#include <ir0/console.h>
#include <ir0/paging.h>
#include <ir0/arch_port.h>
#include <ir0/arch_cpu.h>
#include <ir0/console_backend.h>
#include <ir0/cred.h>
#include <ir0/tty_smoke.h>
#include <d1_12_read_diag.h>
#include <d1_16_tty_read_diag.h>
#include <ir0/errno.h>
#include <ir0/input_backend.h>
#include <ir0/video_console.h>
#include <ir0/process.h>
#include <ir0/sched.h>
#include <ir0/signals.h>
#include <ir0/clock.h>
#include <ir0/clock_wait.h>
#include <string.h>
#include <ir0/spinlock.h>

static void tty_flush_input(void);

/* TCGETS/TCSETS use the Linux x86-64 kernel termios prefix (NCCS=19). */
_Static_assert(sizeof(struct ir0_termios) == 36,
	       "ir0_termios must match Linux uapi termios for TCGETS");

#define IR0_TTY_MAX_READ_WAITERS 8
#define IR0_TTY_ECHO_COLOR       0x07u
#define IR0_TTY_CANON_MAX        256

static process_t *tty_read_waiters[IR0_TTY_MAX_READ_WAITERS];
static struct ir0_termios tty_termios;
static int tty_termios_ready;
static int tty_userspace_attached;
/* Foreground pgrp for /dev/console (TIOCSPGRP); 0 → signal TTY waiters only. */
static int32_t console_fg_pgid;
static pid_t console_ctty_sid;
/* Soft winsize from TIOCSWINSZ (0 → report renderer geometry). */
static uint16_t soft_ws_row;
static uint16_t soft_ws_col;

static char canon_line[IR0_TTY_CANON_MAX];
static size_t canon_line_len;

static char canon_readq[IR0_TTY_CANON_MAX + 1];
static size_t canon_readq_len;
static size_t canon_readq_pos;
/* Set when ICANON VEOF arrives on an empty line — next read returns 0. */
static int canon_eof_pending;

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
		tty_termios.c_cc[IR0_CC_VINTR] = 3;   /* Ctrl+C */
		tty_termios.c_cc[IR0_CC_VQUIT] = 28;  /* Ctrl+\ */
		tty_termios.c_cc[IR0_CC_VERASE] = 127;
		tty_termios.c_cc[IR0_CC_VEOF] = 4;    /* Ctrl+D */
		tty_termios.c_cc[IR0_CC_VTIME] = 0;
		tty_termios.c_cc[IR0_CC_VMIN] = 1;
		tty_termios_ready = 1;
	}
}

static int tty_isig_on(void)
{
	tty_termios_ensure();
	return (tty_termios.c_lflag & IR0_LFLAG_ISIG) ? 1 : 0;
}

/*
 * Linux __isig() (drivers/tty/n_tty.c): VINTR and VQUIT signal the
 * terminal's foreground process group, and nobody when it has none.
 */
static void tty_deliver_sig(int sig)
{
	if (console_fg_pgid > 0)
		(void)send_signal_pgrp(console_fg_pgid, sig);
}

int ir0_console_set_fg_pgid(int32_t pgid)
{
	if (pgid <= 1)
		return -EINVAL;
	console_fg_pgid = pgid;
	return 0;
}

void ir0_console_clear_fg_pgid(int32_t pgid, int32_t exiting_pid)
{
	process_t *p;

	if (pgid <= 1 || console_fg_pgid != pgid)
		return;

	for (p = process_list; p; p = p->next)
	{
		if ((int32_t)p->task.pid == exiting_pid)
			continue;
		if ((int32_t)p->pgid == pgid)
			return;
	}
	console_fg_pgid = 0;
}

void ir0_console_clear_ctty_session(int32_t sid)
{
	if (sid > 0 && console_ctty_sid == (pid_t)sid)
		console_ctty_sid = 0;
}

int ir0_console_has_ctty_for_sid(int32_t sid)
{
	return sid > 0 && console_ctty_sid == (pid_t)sid;
}

int ir0_console_ioctl_set_ctty(void)
{
	int32_t pgid;

	if (!current_process)
		return -ESRCH;

	if (current_process->sid != (pid_t)current_process->task.pid)
		return -EPERM;

	if (console_ctty_sid != 0 &&
	    console_ctty_sid != current_process->sid)
		return -EPERM;

	console_ctty_sid = current_process->sid;

	pgid = current_process->pgid > 0 ? (int32_t)current_process->pgid
					: (int32_t)current_process->task.pid;
	(void)ir0_console_set_fg_pgid(pgid);
	return 0;
}

int32_t ir0_console_get_fg_pgid(void)
{
	if (console_fg_pgid > 0)
		return console_fg_pgid;
	if (current_process)
	{
		if (current_process->pgid > 0)
			return (int32_t)current_process->pgid;
		return (int32_t)current_process->task.pid;
	}
	return 1;
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

/*
 * Input mapping of Linux n_tty_receive_char_special(), the same in
 * canonical and raw mode: IGNCR drops CR, ICRNL turns CR into NL, INLCR
 * turns NL into CR. Returns 0 for a dropped byte.
 */
static char tty_normalize_input(char c)
{
	tty_termios_ensure();
	if (c == '\r')
	{
		if (tty_termios.c_iflag & IR0_IFLAG_IGNCR)
			return 0;
		if (tty_termios.c_iflag & IR0_IFLAG_ICRNL)
			return '\n';
	}
	else if (c == '\n' && (tty_termios.c_iflag & IR0_IFLAG_INLCR))
	{
		return '\r';
	}
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
 * Feed one byte, already mapped by tty_normalize_input(), to the canonical
 * line. Returns 1 when a completed line is queued in canon_readq.
 */
static int tty_canon_feed(char c)
{
	size_t i;
	unsigned char erase;

	tty_termios_ensure();
	erase = tty_termios.c_cc[IR0_CC_VERASE];
	if (erase == 0)
		erase = 127;

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
		/*
		 * Linux ECHONL: echo NL even when ECHO is clear (password prompts).
		 * Without this, "Password:" leaves the cursor on the same line.
		 */
		if (tty_echo_on() || (tty_termios.c_lflag & IR0_LFLAG_ECHONL))
		{
			char nl = '\n';

			console_backend_write(&nl, 1, IR0_TTY_ECHO_COLOR);
		}
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

	/* VEOF (Ctrl+D): empty line → EOF; else push partial line without NL. */
	if ((unsigned char)c == tty_termios.c_cc[IR0_CC_VEOF] ||
	    (unsigned char)c == 4)
	{
		canon_readq_len = 0;
		canon_readq_pos = 0;
		if (canon_line_len == 0)
		{
			canon_eof_pending = 1;
			return 1;
		}
		for (i = 0; i < canon_line_len; i++)
			canon_readq[canon_readq_len++] = canon_line[i];
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

static ir0_spinlock_t tty_lock;

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

static int tty_waiter_evict_stale(void)
{
	int i;
	int n = 0;

	for (i = 0; i < IR0_TTY_MAX_READ_WAITERS; i++)
	{
		process_t *p = tty_read_waiters[i];

		if (!p)
			continue;
		if (p->state != PROCESS_BLOCKED && p->state != PROCESS_READY)
		{
			tty_read_waiters[i] = NULL;
			n++;
		}
	}
	return n;
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

	if (tty_waiter_evict_stale() > 0)
	{
		for (i = 0; i < IR0_TTY_MAX_READ_WAITERS; i++)
		{
			if (tty_read_waiters[i] == NULL)
			{
				tty_read_waiters[i] = p;
				return 1;
			}
		}
	}

	return 0;
}

/*
 * Sleep until input is ready or a signal is pending (Linux
 * wait_event_interruptible on the tty read queue). The waiter registers
 * and marks itself BLOCKED under the IRQ mask before testing for input, so
 * a keyboard IRQ that completes the line in between makes it READY instead
 * of being lost. ir0_console_wake_readers() unhooks a woken waiter; the
 * loop registers again when another reader drained the input first.
 * Returns 0 when the waiter table was full (the caller retries).
 */
static int tty_sleep_for_input(void)
{
	process_t *proc = current_process;
	uint64_t flags;
	int prev_state;
	int ready;

	if (!proc)
		return 0;

	d1_16_tty_read_block(proc, tty_waiter_count(), "tty_input");
	for (;;)
	{
		flags = ir0_spin_lock_irqsave(&tty_lock);
		if (!tty_waiter_register(proc))
		{
			ir0_spin_unlock_irqrestore(&tty_lock, flags);
			klog_notice_fmt("TTY", "[TTY] read-waiter table full (pid=%u)\n",
					(unsigned int)proc->task.pid);
			/* Back off one tick on the clock, then retry the read. */
			(void)ir0_clock_wait_block_until(
				clock_get_uptime_milliseconds() + 1);
			return 0;
		}
		prev_state = proc->state;
		process_set_sched_state(proc, PROCESS_BLOCKED);
		ready = ir0_console_input_ready() || signal_pending(proc);
		ir0_spin_unlock_irqrestore(&tty_lock, flags);
		if (ready)
			break;
		d1_16_tty_state_transition(proc, prev_state, PROCESS_BLOCKED);
		schedule();
	}

	process_set_sched_state(proc, PROCESS_READY);
	tty_waiter_remove(proc);
	d1_16_tty_read_resume(proc, "woke");
	return 1;
}

/*
 * Linux isig() (drivers/tty/n_tty.c): signal the foreground group and,
 * unless NOFLSH, discard all pending input, partial and completed lines.
 * Never schedule() here: keypress runs under IRQ1.
 */
static void tty_isig(int sig)
{
	if (!(tty_termios.c_lflag & IR0_LFLAG_NOFLSH))
		tty_flush_input();
	input_kbd_resync_modifiers();
	tty_deliver_sig(sig);
}

static void tty_input_char(char c)
{
	ir0_console_keypress(c);
}

void ir0_console_keypress(char c)
{
	char nc;
	int line_done;
	unsigned char vintr;
	unsigned char vquit;

	tty_termios_ensure();
	nc = tty_normalize_input(c);
	if (nc == 0)
		return;

	/*
	 * ISIG: Ctrl+C / Ctrl+\ generate signals and are not queued as input.
	 * Without this, Ctrl+C inserted literal 'c' into blocked readers (wc).
	 */
	if (tty_isig_on())
	{
		vintr = tty_termios.c_cc[IR0_CC_VINTR];
		vquit = tty_termios.c_cc[IR0_CC_VQUIT];
		if (vintr == 0)
			vintr = 3;
		if (vquit == 0)
			vquit = 28;
		if ((unsigned char)nc == vintr)
		{
			tty_isig(SIGINT);
			return;
		}
		if ((unsigned char)nc == vquit)
		{
			tty_isig(SIGQUIT);
			return;
		}
	}

	if (tty_icanon_on())
	{
		line_done = tty_canon_feed(nc);
		if (line_done)
		{
			ir0_tty_smoke_line_ready();
			/*
			 * Wake waiters only. Scheduling from IRQ1 (keyboard →
			 * keypress) corrupted prev RIP → #UD into .bss /
			 * process_t during firstboot password Confirm.
			 * the next schedule() picks the READY reader.
			 */
			(void)ir0_console_wake_readers();
		}
		return;
	}

	/* Raw (!ICANON): optional echo; byte lands in the kbd ring via store. */
	if (tty_echo_on())
		tty_echo_char(nc);
}

/*
 * TIOCSTI (Linux drivers/tty/tty_io.c tiocsti()): the byte enters the line
 * discipline as if it had been typed. Allowed on the caller's controlling
 * terminal, or for root (CAP_SYS_ADMIN on Linux). Without a keyboard the
 * console has no raw input queue, so only canonical mode can take it.
 */
int ir0_console_ioctl_sti(const void *user_arg)
{
	char c;

	if (!current_process)
		return -ESRCH;
	if (!ir0_console_has_ctty_for_sid((int32_t)current_process->sid) &&
	    !ir0_cred_is_root())
		return -EPERM;
	if (!user_arg)
		return -EFAULT;
	if (copy_from_user(&c, user_arg, 1) != 0)
		return -EFAULT;
	if (input_kbd_receive(c) == 0)
		return 0;
	if (!tty_icanon_on())
		return -EIO;
	ir0_console_keypress(c);
	return 0;
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

static int64_t tty_read_kernel(char *kbuf, size_t count, int nonblock)
{
	size_t bytes_read = 0;

	if (!kbuf)
		return -EFAULT;
	if (count == 0)
		return 0;

	tty_termios_ensure();

	for (;;)
	{
		/*
		 * Linux n_tty_read() returns available input first and checks
		 * signal_pending() only when it would block: a pending signal
		 * never turns a completed line into EINTR.
		 */
		if (canon_eof_pending && canon_readq_pos >= canon_readq_len)
		{
			canon_eof_pending = 0;
			klog_info_fmt("TTY",
				      "TTY_EOF pid=%x (VEOF empty line)",
				      current_process
					  ? (unsigned)((uint32_t)current_process->task.pid)
					  : 0u);
			return 0;
		}

		while (bytes_read < count && canon_readq_pos < canon_readq_len)
			tty_canon_drain(kbuf, count, &bytes_read);

		if (bytes_read > 0)
			return (int64_t)bytes_read;

		if (!tty_icanon_on())
		{
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
		}

		if (current_process && signal_pending(current_process))
			return -ERESTARTSYS;

		if (tty_icanon_on())
		{
			if (nonblock)
				return -EAGAIN;
			(void)tty_sleep_for_input();
			continue;
		}

		if (nonblock)
			return -EAGAIN;

		{
			unsigned vmin = tty_termios.c_cc[IR0_CC_VMIN];
			unsigned vtime = tty_termios.c_cc[IR0_CC_VTIME];

			/*
			 * Linux non-canonical (termios(3)): VMIN=0,VTIME=0 is a
			 * poll and returns 0 now; VMIN=0,VTIME>0 waits up to VTIME
			 * tenths of a second for a byte, then returns 0.
			 */
			if (vmin == 0 && vtime == 0)
			{
				if (current_process &&
				    signal_pending(current_process))
					return -ERESTARTSYS;
				return 0;
			}
			if (vmin == 0 && vtime > 0)
			{
				uint64_t now = clock_get_uptime_milliseconds();
				uint64_t deadline = now + (uint64_t)vtime * 100u;

				while (clock_get_uptime_milliseconds() < deadline)
				{
					if (input_kbd_has_data())
						break;
					if (current_process &&
					    signal_pending(current_process))
						return -ERESTARTSYS;
					(void)ir0_clock_wait_block_until(deadline);
				}
				continue;
			}
		}

		(void)tty_sleep_for_input();
	}
}

static int64_t tty_write_kernel(const char *kbuf, size_t count, uint8_t color)
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
		int was_icanon = tty_icanon_on();
		int now_icanon;

		/*
		 * Leaving raw: the kbd ring is the raw LD readq. Drain it before
		 * committing ICANON so tty_flush_input() (which only clears the
		 * ring while !ICANON) does not leave stale ESC/NL for the next
		 * raw session. Linux flushes the LD buffer, not i8042, on the
		 * equivalent termios path.
		 */
		now_icanon = (ktermios->c_lflag & IR0_LFLAG_ICANON) ? 1 : 0;
		if (!was_icanon && now_icanon)
			input_kbd_clear();

		/*
		 * Store the caller's termios as given (Linux tty_set_termios()):
		 * no flag is forced on or off here, whatever the mode.
		 */
		tty_termios = *ktermios;
		tty_termios_ready = 1;
		/*
		 * Mode flip or TCSETSF: drop pending LD input so raw↔cooked does
		 * not replay stale NL/ESC bytes (empty-prompt storms).
		 */
		if (request == IR0_CONSOLE_TCSETSF || was_icanon != now_icanon)
		{
			tty_flush_input();
			/*
			 * Password/no-echo prompts can end with modifiers held; TCSETSF
			 * must resync like TCFLSH or the first shell keystrokes show
			 * garbage (ê-prefix) after interactive firstboot login.
			 */
			input_kbd_resync_modifiers();
		}
		return 0;
	}

	return -ENOTTY;
}

int tty_input_bytes_available(void)
{
	tty_termios_ensure();
	if (canon_readq_pos < canon_readq_len)
		return (int)(canon_readq_len - canon_readq_pos);
	if (!tty_icanon_on() && input_kbd_has_data())
		return 1;
	return 0;
}

static void tty_flush_input(void)
{
	/*
	 * Line discipline only. The keyboard ring is the raw (!ICANON) readq;
	 * in cooked mode it is not filled and must not be reset from termios
	 * (TCFLSH/TCSETSF) — that races the IRQ1 producer (login garbage).
	 * Linux n_tty_flush_buffer does not empty i8042.
	 */
	if (!tty_icanon_on())
		input_kbd_clear();
	canon_line_len = 0;
	canon_readq_len = 0;
	canon_readq_pos = 0;
	canon_eof_pending = 0;
}

int ir0_console_wake_readers(void)
{
	int i;
	int woke = 0;
	int waiters_before = tty_waiter_count();
	uint32_t last_pid = 0;

	if (!ir0_console_input_ready())
		return 0;

	/*
	 * Wake one waiter back into tty_read_kernel(), which drains the input
	 * on the reader's own stack inside its read(2), as n_tty_read() does
	 * after wait_woken(). The wake never copies data to the reader.
	 */
	for (i = 0; i < IR0_TTY_MAX_READ_WAITERS; i++)
	{
		process_t *reader;
		int prev_state;

		if (!tty_read_waiters[i])
			continue;

		reader = tty_read_waiters[i];
		if (reader->state == PROCESS_ZOMBIE)
		{
			tty_read_waiters[i] = NULL;
			continue;
		}
		/* Async wake: mark READY; the reader drains on its own stack. */
		prev_state = reader->state;
		process_set_sched_state(reader, PROCESS_READY);
		d1_16_tty_state_transition(reader, prev_state, PROCESS_READY);
		/*
		 * Prefer this reader. IRQ return takes the flag; the keypress
		 * path must not call schedule().
		 */
		sched_promote(reader);
		clock_request_sched_resched();
		last_pid = (uint32_t)reader->task.pid;
		tty_read_waiters[i] = NULL;
		woke = 1;
		break;
	}

	if (woke)
		d1_16_tty_wake(waiters_before, woke, last_pid);

	return woke;
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

static int ir0_console_term_width(void)
{
	int w;

	if (soft_ws_col > 0)
		return (int)soft_ws_col;
	w = console_get_width();
	return w > 0 ? w : 80;
}

static int ir0_console_term_height(void)
{
	int h;

	if (soft_ws_row > 0)
		return (int)soft_ws_row;
	h = console_get_height();
	return h > 0 ? h : 25;
}

int ir0_console_ioctl_winsize(void *user_arg)
{
	struct ir0_winsize win;
	struct console_geometry geo;

	if (!user_arg)
		return -EINVAL;
	console_get_geometry(&geo);
	win.ws_row = (uint16_t)ir0_console_term_height();
	win.ws_col = (uint16_t)ir0_console_term_width();
	{
		unsigned scale = geo.scale ? geo.scale : 1u;
		unsigned cw = geo.cell_width ? geo.cell_width : 8u * scale;
		unsigned ch = geo.cell_height ? geo.cell_height : 16u * scale;

		win.ws_xpixel = (uint16_t)(win.ws_col * cw);
		win.ws_ypixel = (uint16_t)(win.ws_row * ch);
	}
	if (copy_to_user(user_arg, &win, sizeof(win)) != 0)
		return -EFAULT;
	return 0;
}

int ir0_console_ioctl_winsize_set(void *user_arg)
{
	struct ir0_winsize win;
	int32_t pgid;
	int changed;

	if (!user_arg)
		return -EINVAL;
	if (copy_from_user(&win, user_arg, sizeof(win)) != 0)
		return -EFAULT;
	if (win.ws_row == 0 || win.ws_col == 0)
		return -EINVAL;
	changed = soft_ws_row != win.ws_row || soft_ws_col != win.ws_col;
	soft_ws_row = win.ws_row;
	soft_ws_col = win.ws_col;
	if (!changed)
		return 0;
	pgid = ir0_console_get_fg_pgid();
	if (pgid > 1)
		(void)send_signal_pgrp(pgid, SIGWINCH);
	return 0;
}

/*
 * Put the console back in cooked mode with echo when the session that owned
 * it ends (ir0_console_flush_input_session() and session-leader exit). IR0
 * has no tty hangup yet; the next session starts from a usable terminal.
 */
void ir0_console_reset_cooked_echo(void)
{
	tty_termios_ensure();
	tty_termios.c_iflag |= IR0_IFLAG_ICRNL;
	tty_termios.c_lflag |= (IR0_LFLAG_ICANON | IR0_LFLAG_ECHO |
				IR0_LFLAG_ECHOE | IR0_LFLAG_ECHOK |
				IR0_LFLAG_ECHONL);
	tty_termios_ready = 1;
	input_kbd_resync_modifiers();
}

void ir0_console_flush_input(void)
{
	tty_flush_input();
}

void ir0_console_flush_input_session(void)
{
	static int resync_tag;

	tty_flush_input();
	/*
	 * Linux flushes the line discipline on hangup and TCFLSH
	 * (n_tty_flush_buffer()), never the keyboard controller. Here that
	 * means the momentary decoder state (mods + E0/E1) only, never the
	 * ring that IRQ1 fills: a session that ended with a modifier held
	 * would otherwise carry it into the next session's first keystrokes.
	 */
	input_kbd_resync_modifiers();
	if (!resync_tag)
	{
		resync_tag = 1;
		klog_notice_fmt("TTY", "KBD_STATE_RESYNC (session flush)");
	}
}
