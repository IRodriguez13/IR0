/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: conf.h
 * Description: Case API for the IR0 syscall conformance runner. A case is
 *              a function that returns CONF_PASS, CONF_FAIL or CONF_TCONF;
 *              the runner executes it in its own child process and a fresh
 *              working directory, so a crash or a hang becomes BROK for that
 *              case only. Expected values are what Linux returns (man-pages
 *              plus the kernel source); -ENOSYS anywhere means the syscall
 *              is not supported yet and the case reports TCONF.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef IR0_CONFORMANCE_CONF_H
#define IR0_CONFORMANCE_CONF_H

#include "rt.h"

#define CONF_PASS 0
#define CONF_FAIL 1
#define CONF_TCONF 2

/* Absolute path of this program, for the exec cases (probe mode). */
extern const char *conf_self;

/*
 * Nonzero when the runner is the guest's init. Cases with system-wide side
 * effects (kill(-1, ...)) report TCONF elsewhere so a host run stays safe.
 */
extern int conf_guest;

int conf_check(long got, long want, const char *what, int line);
int conf_check_ok(long got, const char *what, int line);
int conf_check_cond(int cond, const char *what, int line);

/* EXPECT(call, want): the call returns exactly want (-errno on error). */
#define CONF_EXPECT_(value, want, text)                                      \
	do                                                                   \
	{                                                                    \
		int conf_r_ = conf_check((long)(value), (long)(want), text,  \
					 __LINE__);                          \
		if (conf_r_)                                                 \
			return conf_r_;                                      \
	} while (0)

/*
 * The arm64 image must fit one IR0 tmpfs file (64 KiB), so it reports the
 * source line only; x86_64 also prints the failing expression.
 */
#ifdef CONF_TERSE
#define CONF_TEXT(x) ""
#else
#define CONF_TEXT(x) #x
#endif

#define EXPECT(call, want) CONF_EXPECT_(call, want, CONF_TEXT(call))
#define EXPECT_ERR(call, err) CONF_EXPECT_(call, -(long)(err), CONF_TEXT(call))

/* OK(var, call): the call succeeds (>= 0); var gets its result. */
#define OK(var, call)                                                        \
	do                                                                   \
	{                                                                    \
		long conf_v_ = (long)(call);                                 \
		int conf_r_ = conf_check_ok(conf_v_, CONF_TEXT(call), __LINE__);       \
		if (conf_r_)                                                 \
			return conf_r_;                                      \
		(var) = conf_v_;                                             \
	} while (0)

#define CHECK(cond)                                                          \
	do                                                                   \
	{                                                                    \
		int conf_r_ = conf_check_cond(!!(cond), CONF_TEXT(cond),       \
					      __LINE__);    \
		if (conf_r_)                                                 \
			return conf_r_;                                      \
	} while (0)

/* Helpers shared by several case files. */
long conf_creat(const char *path, const char *data);
long conf_child_status(long pid, int *status, long timeout_ms);

/* The case list; red selftests build the same runner with their own list. */
#ifndef CONF_CASE_LIST
#define CONF_CASE_LIST "cases.h"
#endif

#define CONF_CASE(name) int name(void);
#include CONF_CASE_LIST

#endif /* IR0_CONFORMANCE_CONF_H */
