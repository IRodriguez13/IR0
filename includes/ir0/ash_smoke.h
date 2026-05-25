/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * FASE58K — compact ash interactive smoke tags (serial, capped).
 *
 * Armed when BusyBox banner appears on stdout after init. Tags are one-shot
 * or lightly capped; no per-character spam.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

int ir0_ash_smoke_active(void);

void ir0_ash_smoke_scan_write(const char *buf, size_t count);
void ir0_ash_smoke_scan_stdout(const char *buf, size_t count);

void ir0_ash_smoke_tty_line_ready(void);
void ir0_ash_smoke_read_return(int fd, int64_t ret);
