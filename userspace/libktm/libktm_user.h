/**
 * IR0 userspace — libktm-user
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: libktm_user.h
 * Description: Thin wrapper over /dev/ktm ioctls.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#include <ir0/ktm/uapi.h>

int ktm_open(void);
void ktm_close(int fd);

int ktm_case_begin(int fd, const char *name);
int ktm_case_end(int fd, const char *name, int result);

int ktm_assert_eq_u64(int fd, const char *name, uint64_t expected, uint64_t actual);
int ktm_assert_true(int fd, const char *name, int cond);

int ktm_checkpoint(int fd, const char *name);
int ktm_snapshot_request(int fd, ktm_ioc_snapshot_t *out);
int ktm_run_invariants(int fd, uint32_t mask);
int ktm_run_scenario(int fd, const char *name, int32_t *result_out);
int ktm_get_caps(int fd, ktm_user_caps_t *out);
int ktm_reset(int fd);
