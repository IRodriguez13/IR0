/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 console / TTY facade — keyboard input, text output, minimal termios.
 *
 * Used by devfs /dev/console and /dev/tty backends. Syscall layer copies
 * user buffers; this facade operates on kernel buffers only.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#define IR0_CONSOLE_TCGETS 0x5401u
#define IR0_CONSOLE_TCSETS 0x5402u

/*
 * Linux-compatible termios subset (musl/BusyBox isatty uses TCGETS).
 */
typedef unsigned int ir0_tcflag_t;
typedef unsigned char ir0_cc_t;

#define IR0_NCCS 32

struct ir0_termios
{
	ir0_tcflag_t c_iflag;
	ir0_tcflag_t c_oflag;
	ir0_tcflag_t c_cflag;
	ir0_tcflag_t c_lflag;
	ir0_cc_t c_line;
	ir0_cc_t c_cc[IR0_NCCS];
	uint32_t c_ispeed;
	uint32_t c_ospeed;
};

#define IR0_CONSOLE_IFLAG_DEFAULT (0x00000400u) /* ICRNL */
#define IR0_CONSOLE_OFLAG_DEFAULT (0x00000005u) /* ONLCR|OPOST */
#define IR0_CONSOLE_CFLAG_DEFAULT (0x00004B00u) /* CS8|CREAD|HUPCL */
#define IR0_CONSOLE_LFLAG_DEFAULT (0x00000009u) /* ISIG|ECHO */
#define IR0_LFLAG_ECHO            (0x00000008u)
#define IR0_IFLAG_ICRNL           (0x00000400u)
#define IR0_OFLAG_ONLCR           (0x00000004u)
#define IR0_OFLAG_OPOST           (0x00000001u)

void ir0_console_wake_readers(void);
int ir0_console_take_resched(void);
int ir0_console_poll(void);
int ir0_console_has_blocked_reader(void);
struct process;
void ir0_console_purge_waiters_for_process(struct process *p);

void ir0_console_input_enqueue(char c);
void ir0_console_drain_echo(void);
void ir0_console_on_userspace_attach(void);

int64_t ir0_console_read(void *kbuf, size_t count, int nonblock);
int64_t ir0_console_write(const void *kbuf, size_t count, uint8_t color);
int ir0_console_isatty(void);
int ir0_console_fill_termios(struct ir0_termios *out);
int ir0_console_set_termios(const struct ir0_termios *in);
