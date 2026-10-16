/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: portable_string.c
 * Description: Freestanding string primitives for the ARM64 bring-up image.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <stddef.h>

size_t strlen(const char *s)
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

void *memcpy(void *dst, const void *src, size_t n)
{
	unsigned char *d = (unsigned char *)dst;
	const unsigned char *s = (const unsigned char *)src;

	while (n--)
	{
		*d++ = *s++;
	}
	return dst;
}

void *memset(void *dst, int value, size_t n)
{
	unsigned char *d = (unsigned char *)dst;

	while (n--)
	{
		*d++ = (unsigned char)value;
	}
	return dst;
}

int strcmp(const char *a, const char *b)
{
	while (*a && *a == *b)
	{
		a++;
		b++;
	}
	return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
	while (n && *a && *a == *b)
	{
		a++;
		b++;
		n--;
	}
	if (!n)
	{
		return 0;
	}
	return (unsigned char)*a - (unsigned char)*b;
}

char *strchr(const char *s, int c)
{
	char ch = (char)c;

	if (!s)
		return NULL;
	while (*s)
	{
		if (*s == ch)
			return (char *)s;
		s++;
	}
	return ch == '\0' ? (char *)s : NULL;
}

char *strrchr(const char *s, int c)
{
	const char *last = NULL;
	char ch = (char)c;

	if (!s)
		return NULL;
	while (*s)
	{
		if (*s == ch)
			last = s;
		s++;
	}
	if (ch == '\0')
		return (char *)s;
	return (char *)last;
}

char *strncat(char *dst, const char *src, size_t n)
{
	char *d = dst;

	if (!dst || !src)
		return dst;
	while (*d)
		d++;
	while (n && *src)
	{
		*d++ = *src++;
		n--;
	}
	*d = '\0';
	return dst;
}
