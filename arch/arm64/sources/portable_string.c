/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: portable_string.c
 * Description: Freestanding strlen/memcpy for ARM64 portable slice (no oops.h).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <stddef.h>

size_t arm64_portable_strlen(const char *s)
{
	size_t n = 0;

	if (!s)
	{
		return 0;
	}
	while (s[n])
	{
		n++;
	}
	return n;
}

void *arm64_portable_memcpy(void *dst, const void *src, size_t n)
{
	unsigned char *d = (unsigned char *)dst;
	const unsigned char *s = (const unsigned char *)src;

	while (n--)
	{
		*d++ = *s++;
	}
	return dst;
}
