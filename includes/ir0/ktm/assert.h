/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: assert.h
 * Description: KTM assertions (domain + generic).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/ktm/snapshot.h>
#include <stdbool.h>

#ifndef KTM_FILE
#define KTM_FILE \
	(__builtin_strrchr(__FILE__, '/') ? __builtin_strrchr(__FILE__, '/') + 1 : __FILE__)
#endif

void ktm_assert_result(bool pass, const char *expr, const char *file,
		       unsigned line, const char *message);

/* Collapse N happy-path asserts into one ASSERT_BATCH transport line. */
void ktm_assert_batch_begin(const char *name);
void ktm_assert_batch_end(void);

#define KTM_V1_ASSERT_TRUE(expr) \
	ktm_assert_result(!!(expr), #expr, KTM_FILE, __LINE__, NULL)

#define KTM_V1_ASSERT_EQ(a, b) \
	ktm_assert_result(((a) == (b)), #a " == " #b, KTM_FILE, __LINE__, NULL)

#define KTM_REQUIRE(expr) do { \
	if (!(expr)) { \
		ktm_assert_result(false, #expr, KTM_FILE, __LINE__, "REQUIRE"); \
		ktm_assert_batch_end(); \
		return -1; \
	} \
} while (0)

void ktm_assert_no_process_leak(const ktm_system_snapshot_t *before,
				const ktm_system_snapshot_t *after);
void ktm_assert_no_frame_leak(const ktm_system_snapshot_t *before,
			      const ktm_system_snapshot_t *after);

#define KTM_ASSERT_NO_PROCESS_LEAK(b, a) ktm_assert_no_process_leak((b), (a))
#define KTM_ASSERT_NO_FRAME_LEAK(b, a)   ktm_assert_no_frame_leak((b), (a))
