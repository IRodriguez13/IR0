/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: stat_user.h
 * Description: Linux x86_64 / musl struct stat layout for syscall ABI copies
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#include <ir0/stat.h>

/*
 * musl arch/x86_64/bits/stat.h — field order differs from kernel stat_t.
 * Source: musl 1.2.x uapi (st_nlink before st_mode; timespec timestamps).
 */
struct ir0_user_stat
{
    uint64_t st_dev;
    uint64_t st_ino;
    unsigned long st_nlink;

    unsigned st_mode;
    unsigned st_uid;
    unsigned st_gid;
    unsigned __pad0;
    uint64_t st_rdev;
    int64_t st_size;
    int64_t st_blksize;
    int64_t st_blocks;

    struct
    {
        int64_t tv_sec;
        int64_t tv_nsec;
    } st_atim;
    struct
    {
        int64_t tv_sec;
        int64_t tv_nsec;
    } st_mtim;
    struct
    {
        int64_t tv_sec;
        int64_t tv_nsec;
    } st_ctim;
    int64_t __unused[3];
};

#define IR0_USER_STAT_SIZE ((unsigned)sizeof(struct ir0_user_stat))

void ir0_stat_to_user(const stat_t *kst, struct ir0_user_stat *ust);

int ir0_copy_stat_to_user(void *user_buf, const stat_t *kst);
