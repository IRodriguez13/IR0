/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: linux_abi_tty.c
 * Description: Console line discipline against Linux drivers/tty/n_tty.c:
 *              termios is stored as given, ICRNL is honoured as set, VINTR
 *              signals only the foreground process group and flushes input
 *              unless NOFLSH, and neither signal delivery nor execve(2)
 *              touches the terminal. Input comes from TIOCSTI. One case per
 *              divergence; each passes on Linux with the runner as init.
 *              Built with the conformance runner
 *              (make ir0-selftest-tty-abi-{x86,arm64}).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "conf.h"

/* asm-generic/ioctls.h and termbits.h: the same values on x86_64 and arm64. */
#define T_TCGETS   0x5401
#define T_TCSETS   0x5402
#define T_TCFLSH   0x540B
#define T_TIOCSTI  0x5412
#define T_FIONREAD 0x541B
#define T_TCIFLUSH 0

#define T_INLCR  0000100u
#define T_IGNCR  0000200u
#define T_ICRNL  0000400u
#define T_ISIG   0000001u
#define T_ICANON 0000002u
#define T_ECHO   0000010u
#define T_NOFLSH 0000200u
#define T_VINTR  0

#define WAIT_MS 5000L

/* Kernel struct termios (TCGETS/TCSETS), NCCS = 19. */
struct kterm
{
	unsigned int c_iflag;
	unsigned int c_oflag;
	unsigned int c_cflag;
	unsigned int c_lflag;
	unsigned char c_line;
	unsigned char c_cc[19];
};

static struct kterm g_saved;
static volatile int g_hits;

static void on_signal(int sig)
{
	(void)sig;
	g_hits++;
}

static long tty_get(struct kterm *t)
{
	return S3(SYS_ioctl, 0, T_TCGETS, t);
}

static long tty_set(const struct kterm *t)
{
	return S3(SYS_ioctl, 0, T_TCSETS, t);
}

static long tty_avail(void)
{
	int n = -1;
	long r = S3(SYS_ioctl, 0, T_FIONREAD, &n);

	return r != 0 ? r : n;
}

static long tty_sti(const char *s)
{
	long r;

	for (; *s; s++)
	{
		r = S3(SYS_ioctl, 0, T_TIOCSTI, s);
		if (r != 0)
			return r;
	}
	return 0;
}

/*
 * Put the console in canonical mode with @lflag (no echo, so injected bytes
 * stay off the log) and an empty input queue; the original termios comes
 * back in tty_case_end().
 */
static long tty_case_begin(unsigned int iflag_clear, unsigned int lflag)
{
	struct kterm t;
	long r;

	r = tty_get(&g_saved);
	if (r != 0)
		return r;
	t = g_saved;
	t.c_iflag &= ~iflag_clear;
	t.c_lflag = lflag;
	t.c_cc[T_VINTR] = 3;
	r = tty_set(&t);
	if (r != 0)
		return r;
	return S3(SYS_ioctl, 0, T_TCFLSH, T_TCIFLUSH);
}

static int tty_case_end(int verdict)
{
	(void)S3(SYS_ioctl, 0, T_TCFLSH, T_TCIFLUSH);
	(void)tty_set(&g_saved);
	return verdict;
}

/* drivers/tty/tty_io.c tiocsti(): the byte enters the line discipline. */
static int tiocsti_queues_input(void)
{
	char buf[8];

	EXPECT(tty_case_begin(0, T_ICANON), 0);
	EXPECT(tty_sti("ok\n"), 0);
	EXPECT(tty_avail(), 3);
	EXPECT(S3(SYS_read, 0, buf, sizeof(buf)), 3);
	CHECK(rt_memeq(buf, "ok\n", 3));
	return CONF_PASS;
}

int tty_tiocsti_queues_input(void)
{
	return tty_case_end(tiocsti_queues_input());
}

/*
 * drivers/tty/tty_ioctl.c set_termios() stores the caller's termios as
 * given: canonical mode does not bring ICRNL back.
 */
static int tcsets_keeps_icrnl_clear(void)
{
	struct kterm got;

	EXPECT(tty_case_begin(T_ICRNL, T_ICANON), 0);
	EXPECT(tty_get(&got), 0);
	EXPECT(got.c_iflag & T_ICRNL, 0);
	return CONF_PASS;
}

int tty_tcsets_keeps_icrnl_clear(void)
{
	return tty_case_end(tcsets_keeps_icrnl_clear());
}

/*
 * n_tty_receive_char_special(): CR becomes NL only under ICRNL. Without
 * it a canonical line keeps CR as data and ends at NL.
 */
static int canon_cr_is_data_without_icrnl(void)
{
	char buf[16];

	EXPECT(tty_case_begin(T_ICRNL | T_IGNCR | T_INLCR, T_ICANON), 0);
	EXPECT(tty_sti("a\rb\n"), 0);
	EXPECT(tty_avail(), 4);
	EXPECT(S3(SYS_read, 0, buf, sizeof(buf)), 4);
	CHECK(rt_memeq(buf, "a\rb\n", 4));
	return CONF_PASS;
}

int tty_canon_cr_is_data_without_icrnl(void)
{
	return tty_case_end(canon_cr_is_data_without_icrnl());
}

/*
 * n_tty.c __isig(): VINTR signals the terminal's foreground process
 * group. A terminal nobody made a controlling terminal has none, so the
 * character signals nobody, the process that typed it included.
 */
static int vintr_without_foreground_signals_nobody(void)
{
	int i;

	EXPECT(rt_sigaction(SIGINT, on_signal, 0, 0), 0);
	EXPECT(tty_case_begin(0, T_ISIG | T_ICANON), 0);
	EXPECT(tty_sti("\003"), 0);
	for (i = 0; i < 8; i++)
		(void)S0(SYS_getpid);
	EXPECT(g_hits, 0);
	return CONF_PASS;
}

int tty_vintr_without_foreground_signals_nobody(void)
{
	return tty_case_end(vintr_without_foreground_signals_nobody());
}

/* n_tty.c isig(): without NOFLSH, VINTR also discards queued input. */
static int vintr_flushes_input(void)
{
	EXPECT(rt_sigaction(SIGINT, SIG_IGN, 0, 0), 0);
	EXPECT(tty_case_begin(0, T_ISIG | T_ICANON), 0);
	EXPECT(tty_sti("abc\n"), 0);
	EXPECT(tty_avail(), 4);
	EXPECT(tty_sti("\003"), 0);
	EXPECT(tty_avail(), 0);
	return CONF_PASS;
}

int tty_vintr_flushes_input(void)
{
	return tty_case_end(vintr_flushes_input());
}

/* n_tty.c isig(): with NOFLSH, VINTR only signals; the input stays. */
static int vintr_noflsh_keeps_input(void)
{
	EXPECT(rt_sigaction(SIGINT, SIG_IGN, 0, 0), 0);
	EXPECT(tty_case_begin(0, T_ISIG | T_ICANON | T_NOFLSH), 0);
	EXPECT(tty_sti("abc\n"), 0);
	EXPECT(tty_sti("\003"), 0);
	EXPECT(tty_avail(), 4);
	return CONF_PASS;
}

int tty_vintr_noflsh_keeps_input(void)
{
	return tty_case_end(vintr_noflsh_keeps_input());
}

/*
 * kernel/signal.c delivers and returns from a handler without touching any
 * terminal: a SIGINT sent with kill(2) leaves the queued line in place.
 */
static int caught_signal_keeps_input(void)
{
	EXPECT(rt_sigaction(SIGINT, on_signal, 0, 0), 0);
	EXPECT(tty_case_begin(0, T_ICANON), 0);
	EXPECT(tty_sti("abc\n"), 0);
	EXPECT(S2(SYS_kill, S0(SYS_getpid), SIGINT), 0);
	EXPECT(g_hits, 1);
	EXPECT(tty_avail(), 4);
	return CONF_PASS;
}

int tty_caught_signal_keeps_input(void)
{
	return tty_case_end(caught_signal_keeps_input());
}

/* fs/exec.c: execve(2) leaves the terminal's termios as the caller set it. */
static int exec_keeps_termios(void)
{
	char num[24];
	char *argv[5];
	int status = 0;
	long pid;

	EXPECT(tty_case_begin(0, T_ICANON), 0);
	num[0] = '\0';
	rt_catn(num, sizeof(num), (long)T_ECHO);
	argv[0] = (char *)conf_self;
	argv[1] = (char *)"--probe";
	argv[2] = (char *)"lflag_clear";
	argv[3] = num;
	argv[4] = 0;
	pid = rt_fork();
	CHECK(pid >= 0);
	if (pid == 0)
	{
		(void)S3(SYS_execve, conf_self, argv, 0);
		rt_exit(121);
	}
	EXPECT(conf_child_status(pid, &status, WAIT_MS), pid);
	CHECK(RT_EXITED(status));
	EXPECT(RT_EXITCODE(status), 0);
	return CONF_PASS;
}

int tty_exec_keeps_termios(void)
{
	return tty_case_end(exec_keeps_termios());
}
