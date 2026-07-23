/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: posix_shm.h
 * Description: POSIX shm_open path via /dev/shm named memfd.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <ir0/types.h>

int posix_shm_path_is(const char *path);
int64_t posix_shm_try_open(const char *path, int flags, mode_t mode);
int posix_shm_try_unlink(const char *path);
void posix_shm_ensure_dir(void);
