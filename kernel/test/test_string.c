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

	KTEST_END();
}
