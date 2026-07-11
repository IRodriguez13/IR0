/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: init_prlimit_smoke.c
 * Description: getrlimit/prlimit64 RLIMIT_NOFILE roundtrip.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <sys/resource.h>
#include <unistd.h>

static void tag(const char *s)
{
	const char *p = s;

	while (*p)
		p++;
	(void)write(1, s, (size_t)(p - s));
}

int main(void)
{
	struct rlimit old_lim;
	struct rlimit new_lim;
	struct rlimit check;

	if (getrlimit(RLIMIT_NOFILE, &old_lim) != 0)
		return 2;
	new_lim = old_lim;
	if (new_lim.rlim_cur > 16)
		new_lim.rlim_cur = 16;
	if (setrlimit(RLIMIT_NOFILE, &new_lim) != 0)
		return 3;
	if (getrlimit(RLIMIT_NOFILE, &check) != 0)
		return 4;
	if (check.rlim_cur != new_lim.rlim_cur)
		return 5;
	(void)setrlimit(RLIMIT_NOFILE, &old_lim);
	tag("PRLIMIT_OK\n");
	return 0;
}
