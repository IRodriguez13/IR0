/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ksft.h
 * Description: Minimal userspace KTAP helpers for IR0 selftests.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef IR0_KSFT_H
#define IR0_KSFT_H

#ifndef IR0_SELFTEST_NOLIBC
#include <unistd.h>
#endif

#define KSFT_PASS 0
#define KSFT_FAIL 1
#define KSFT_SKIP 4

struct ksft_state
{
	unsigned int planned;
	unsigned int current;
	unsigned int failed;
	unsigned int skipped;
};

static struct ksft_state g_ksft;

static unsigned int ksft_u32_to_dec(unsigned int value, char *out)
{
	char tmp[10];
	unsigned int n = 0;
	unsigned int i;

	if (value == 0)
	{
		out[0] = '0';
		return 1;
	}
	while (value != 0 && n < sizeof(tmp))
	{
		tmp[n++] = (char)('0' + (value % 10U));
		value /= 10U;
	}
	for (i = 0; i < n; i++)
		out[i] = tmp[n - 1U - i];
	return n;
}

static void ksft_write_lit(const char *s)
{
	size_t n = 0;

	while (s[n] != '\0')
		n++;
	(void)write(1, s, n);
}

static void ksft_set_plan(unsigned int count)
{
	char nbuf[12];
	unsigned int nlen;

	g_ksft.planned = count;
	g_ksft.current = 0;
	g_ksft.failed = 0;
	g_ksft.skipped = 0;
	nlen = ksft_u32_to_dec(count, nbuf);
	ksft_write_lit("KTAP version 1\n1..");
	(void)write(1, nbuf, nlen);
	ksft_write_lit("\n");
}

static void ksft_test_result(int pass, const char *name)
{
	char nbuf[12];
	unsigned int nlen;

	g_ksft.current++;
	if (!pass)
		g_ksft.failed++;
	nlen = ksft_u32_to_dec(g_ksft.current, nbuf);
	ksft_write_lit(pass ? "ok " : "not ok ");
	(void)write(1, nbuf, nlen);
	ksft_write_lit(" ");
	ksft_write_lit(name);
	ksft_write_lit("\n");
}

static int ksft_exit(void)
{
	if (g_ksft.planned != 0 && g_ksft.current != g_ksft.planned)
		g_ksft.failed++;
	if (g_ksft.failed)
		return KSFT_FAIL;
	if (g_ksft.skipped && g_ksft.skipped == g_ksft.current)
		return KSFT_SKIP;
	return KSFT_PASS;
}

#endif /* IR0_KSFT_H */
