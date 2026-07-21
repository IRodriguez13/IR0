/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ir0_smoke_tag.h
 * Description: Userspace smoke/autokill tags (runit stages, runsv).
 * Equivalent to kernel klog_smoke(): bare token + newline on stdout (fd 1),
 * greppable by scripts/smoke_autokill.py and Makefile --done tags.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <unistd.h>

static inline void ir0_smoke_tag(const char *s)
{
	const char *p = s;

	if (!s)
		return;
	while (*p)
		p++;
	(void)write(1, s, (size_t)(p - s));
}
