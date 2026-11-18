/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Capped serial smoke tags for the console TTY path (ir0.tty_smoke=1).
 * Generic line-discipline and read(2) events only; the harness derives what
 * a program did from the serial log.
 */

#pragma once

#include <stdint.h>

int ir0_tty_smoke_active(void);

/* A canonical line is complete and waits for a reader. */
void ir0_tty_smoke_line_ready(void);
/* read(2) on @fd returned @ret (tagged for fd 0 with data). */
void ir0_tty_smoke_read_return(int fd, int64_t ret);
