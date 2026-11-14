/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_string.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Tests de string (strlen, strcmp) sin proceso.
 */

#include "test/ktest_harness.h"
#include <string.h>

void ktest_string(void)
{
	char buf[40];
	char src[64];
	char dst[64];

	KTEST_BEGIN("string");

	KASSERT_EQ(strlen(""), (size_t)0);
	KASSERT_EQ(strlen("a"), (size_t)1);
	KASSERT_EQ(strlen("abc"), (size_t)3);

	KASSERT_EQ(strcmp("", ""), 0);
	KASSERT_EQ(strcmp("a", "a"), 0);
	KASSERT(strcmp("a", "b") < 0);
	KASSERT(strcmp("b", "a") > 0);
	KASSERT(strcmp("ab", "abc") < 0);
	KASSERT(strcmp("abc", "ab") > 0);

	/*
	 * snprintf("%d") is the one decimal formatter; pin its contract,
	 * including INT_MIN (negating it overflows).
	 */
	snprintf(buf, sizeof(buf), "%d", 0);
	KASSERT(strcmp(buf, "0") == 0);
	snprintf(buf, sizeof(buf), "%d", -42);
	KASSERT(strcmp(buf, "-42") == 0);
	snprintf(buf, sizeof(buf), "%d", -2147483647 - 1);
	KASSERT(strcmp(buf, "-2147483648") == 0);
	snprintf(buf, sizeof(buf), "%x", -1);
	KASSERT(strcmp(buf, "ffffffff") == 0);

	/*
	 * Width, precision, and left alignment: devfs prints "%-20s".
	 * Every field is bounded by the destination size as well.
	 */
	snprintf(buf, sizeof(buf), "[%-20s]", "hda");
	KASSERT(strcmp(buf, "[hda                 ]") == 0);
	snprintf(buf, sizeof(buf), "[%20s]", "hda");
	KASSERT(strcmp(buf, "[                 hda]") == 0);
	snprintf(buf, sizeof(buf), "%.3s", "abcdef");
	KASSERT(strcmp(buf, "abc") == 0);
	snprintf(buf, sizeof(buf), "%8.3s", "abcdef");
	KASSERT(strcmp(buf, "     abc") == 0);
	snprintf(buf, sizeof(buf), "%-8.3s", "abcdef");
	KASSERT(strcmp(buf, "abc     ") == 0);
	snprintf(buf, sizeof(buf), "%5c", 'Z');
	KASSERT(strcmp(buf, "    Z") == 0);
	snprintf(buf, sizeof(buf), "%08d", -42);
	KASSERT(strcmp(buf, "-0000042") == 0);
	snprintf(buf, sizeof(buf), "100%%");
	KASSERT(strcmp(buf, "100%") == 0);
	snprintf(buf, sizeof(buf), "%");
	KASSERT(strcmp(buf, "%") == 0);

	for (int i = 0; i < (int)sizeof(src); i++)
		src[i] = (char)(i + 1);

	memset(dst, 0, sizeof(dst));
	KASSERT(memset(dst, 0x5a, 7) == dst);
	for (int i = 0; i < 7; i++)
		KASSERT_EQ((unsigned char)dst[i], (unsigned char)0x5a);

	KASSERT(memcpy(dst, src, sizeof(dst)) == dst);
	KASSERT(memcmp(dst, src, sizeof(dst)) == 0);
	KASSERT(memcpy(dst, src, 0) == dst);

	/* Overlap both ways: memmove must copy as if through a temporary. */
	KASSERT(memmove(src + 1, src, 31) == src + 1);
	for (int i = 0; i < 31; i++)
		KASSERT_EQ((unsigned char)src[i + 1], (unsigned char)(i + 1));

	KASSERT(memmove(src, src + 1, 31) == src);
	for (int i = 0; i < 31; i++)
		KASSERT_EQ((unsigned char)src[i], (unsigned char)(i + 1));
	KASSERT(memcpy(dst, src, sizeof(src)) == dst);
	KASSERT(memmove(src, src, sizeof(src)) == src);
	KASSERT(memcmp(src, dst, sizeof(src)) == 0);

	/* Word paths must not touch bytes outside the requested range. */
	{
		unsigned char area[24];
		int i;

		for (i = 0; i < (int)sizeof(area); i++)
			area[i] = 0xa5;
		memset(area + 3, 0x5a, 10);
		for (i = 0; i < 3; i++)
			KASSERT_EQ(area[i], 0xa5);
		for (i = 3; i < 13; i++)
			KASSERT_EQ(area[i], 0x5a);
		for (i = 13; i < (int)sizeof(area); i++)
			KASSERT_EQ(area[i], 0xa5);
	}
	{
		unsigned char guarded_src[24];
		unsigned char guarded_dst[24];
		int i;

		for (i = 0; i < (int)sizeof(guarded_src); i++)
			guarded_src[i] = (unsigned char)(0x30 + i);
		memset(guarded_dst, 0xa5, sizeof(guarded_dst));
		KASSERT(memcpy(guarded_dst + 5, guarded_src + 2, 11) ==
			guarded_dst + 5);
		KASSERT(memcmp(guarded_dst + 5, guarded_src + 2, 11) == 0);
		for (i = 0; i < 5; i++)
			KASSERT_EQ(guarded_dst[i], 0xa5);
		for (i = 16; i < (int)sizeof(guarded_dst); i++)
			KASSERT_EQ(guarded_dst[i], 0xa5);
	}

	{
		char *end;
		const char invalid_prefix[] = "0x";

		KASSERT_EQ(strtoul("0x2a", &end, 0), 42UL);
		KASSERT(*end == '\0');
		KASSERT_EQ(strtoul("19", &end, 8), 1UL);
		KASSERT(*end == '9');
		KASSERT_EQ(strtoul(invalid_prefix, &end, 0), 0UL);
		KASSERT(end == invalid_prefix + 1);
		KASSERT_EQ(strtoul("999999999999999999999999999999", &end, 10),
			   ~0UL);
		KASSERT(*end == '\0');
	}

	KTEST_END();
}
