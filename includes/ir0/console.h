/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 console / TTY facade — keyboard input, text output, minimal termios.
 *
 * Syscall/devfs copy user buffers; TTY operates on kernel buffers only.
 * devfs_console_ioctl copies struct ir0_termios once, then calls
 * tty_ioctl_termios_kernel() with a kernel struct.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#define IR0_CONSOLE_TCGETS  0x5401u
#define IR0_CONSOLE_TCSETS  0x5402u
#define IR0_CONSOLE_TCSETSW 0x5403u
#define IR0_CONSOLE_TCSETSF 0x5404u
#define IR0_CONSOLE_TIOCGWINSZ 0x5413u
/* Linux TIOCGPTN — get pty number (_IOR('T', 0x30, unsigned int)) */
#define IR0_TIOCGPTN 0x80045430u
/* Linux TIOCSPTLCK — lock/unlock pty (_IOW('T', 0x31, int)); no-op OK */
#define IR0_TIOCSPTLCK 0x40045431u

typedef unsigned int ir0_tcflag_t;
typedef unsigned char ir0_cc_t;

#define IR0_NCCS 32

struct ir0_winsize
{
	uint16_t ws_row;
	uint16_t ws_col;
	uint16_t ws_xpixel;
	uint16_t ws_ypixel;
};

struct ir0_termios
{
	ir0_tcflag_t c_iflag;
	ir0_tcflag_t c_oflag;
	ir0_tcflag_t c_cflag;
	ir0_tcflag_t c_lflag;
	ir0_cc_t c_line;
	uint8_t __pad[3];
	ir0_cc_t c_cc[IR0_NCCS];
	uint32_t c_ispeed;
	uint32_t c_ospeed;
};

#define IR0_CONSOLE_IFLAG_DEFAULT (0x00000400u) /* ICRNL */
#define IR0_CONSOLE_OFLAG_DEFAULT (0x00000005u) /* ONLCR|OPOST */
#define IR0_CONSOLE_CFLAG_DEFAULT (0x00004B00u) /* CS8|CREAD|HUPCL */
#define IR0_CONSOLE_LFLAG_DEFAULT (0x0000003Bu) /* ISIG|ICANON|ECHO|ECHOE|ECHOK */
#define IR0_LFLAG_ISIG             (0x00000001u)
#define IR0_LFLAG_ICANON           (0x00000002u)
#define IR0_LFLAG_ECHO             (0x00000008u)
#define IR0_LFLAG_ECHOE            (0x00000010u)
#define IR0_LFLAG_ECHOK            (0x00000020u)
#define IR0_IFLAG_ICRNL           (0x00000400u)
#define IR0_OFLAG_ONLCR           (0x00000004u)
#define IR0_OFLAG_OPOST           (0x00000001u)
#define IR0_CC_VEOF               4
#define IR0_CC_VTIME              5
#define IR0_CC_VMIN               6
#define IR0_CC_VERASE             2

/* TTY line discipline (kernel buffers only) */
void tty_input_char(char c);
int64_t tty_read_kernel(char *kbuf, size_t count, int nonblock);
int64_t tty_write_kernel(const char *kbuf, size_t count, uint8_t color);
int tty_ioctl_termios_kernel(uint64_t request, struct ir0_termios *ktermios);
void tty_flush_input(void);

int ir0_console_wake_readers(void);
int ir0_console_take_resched(void);
int ir0_console_poll(void);
int ir0_console_has_blocked_reader(void);
struct process;
void ir0_console_purge_waiters_for_process(struct process *p);

void ir0_console_input_enqueue(char c);
void ir0_console_keypress(char c);
int ir0_console_input_ready(void);
int ir0_console_store_key_in_ring(void);
void ir0_console_drain_echo(void);
void ir0_console_on_userspace_attach(void);
int ir0_console_in_userspace(void);

int64_t ir0_console_read(void *kbuf, size_t count, int nonblock);
int64_t ir0_console_write(const void *kbuf, size_t count, uint8_t color);
int ir0_console_isatty(void);
int ir0_console_term_width(void);
int ir0_console_term_height(void);
int ir0_console_ioctl_winsize(void *user_arg);
int ir0_console_fill_termios(struct ir0_termios *out);
int ir0_console_set_termios(const struct ir0_termios *in);
void ir0_console_flush_input(void);
