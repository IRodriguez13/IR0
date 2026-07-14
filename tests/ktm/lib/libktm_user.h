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

/* Arm a named fault point (requires KTM_CAP_FAULT). Modes: KTM_FAULT_MODE_*. */
int ktm_config_fault(int fd, const char *name, uint32_t mode, uint32_t value,
		     uint32_t seed);

/*
 * Pure snapshot delta helpers (no ioctl). Return 0 if no leak growth.
 * processes: after.processes <= before.processes
 * frames: after.used_frames <= before.used_frames
 * pipes: after.pipes <= before.pipes
 */
int ktm_snapshot_no_process_leak(const ktm_ioc_snapshot_t *before,
				 const ktm_ioc_snapshot_t *after);
int ktm_snapshot_no_frame_leak(const ktm_ioc_snapshot_t *before,
			       const ktm_ioc_snapshot_t *after);
int ktm_snapshot_no_pipe_leak(const ktm_ioc_snapshot_t *before,
			      const ktm_ioc_snapshot_t *after);

/* Fill signed deltas (after - before). Returns -1 on null args. */
int ktm_snapshot_delta(const ktm_ioc_snapshot_t *before,
		       const ktm_ioc_snapshot_t *after,
		       ktm_ioc_snapshot_t *delta_out);

/*
 * Take after snapshot, assert no process/frame/pipe leak via /dev/ktm USER_ASSERT.
 * Returns number of failed checks (0 = OK).
 */
int ktm_assert_no_leaks(int fd, const ktm_ioc_snapshot_t *before);

/*
 * Write a short result file under virtio-9p /mnt/host/<relpath>.
 * Tolerates share already mounted by init_hostshare_exec stub (no remount).
 * Prints KTM_HOSTSHARE_REPORT_OK on success.
 */
int ktm_hostshare_report(const char *relpath, const char *payload);
