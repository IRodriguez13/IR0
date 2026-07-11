/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: snapshot.h
 * Description: KTM comparable system snapshots.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

typedef struct ktm_system_snapshot
{
	uint64_t total_frames;
	uint64_t used_frames;
	uint64_t free_frames;
	uint64_t processes;
	uint64_t zombies;
	uint64_t open_fds;
	uint64_t pipes;
} ktm_system_snapshot_t;

typedef struct ktm_snapshot_delta
{
	int64_t used_frames;
	int64_t processes;
	int64_t zombies;
	int64_t open_fds;
	int64_t pipes;
} ktm_snapshot_delta_t;

int ktm_snapshot_take(ktm_system_snapshot_t *out);
void ktm_snapshot_diff(const ktm_system_snapshot_t *before,
		       const ktm_system_snapshot_t *after,
		       ktm_snapshot_delta_t *delta);
