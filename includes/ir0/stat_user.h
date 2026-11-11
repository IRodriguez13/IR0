/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: stat_user.h
 * Description: Linux uapi struct stat for syscall ABI copies (per ISA)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#include <ir0/stat.h>

/*
 * struct ir0_user_stat is the ISA's Linux uapi struct stat: x86_64 has its
 * own layout, arm64 the asm-generic one. The build selects it.
 */
#include <asm/stat.h>

#define IR0_USER_STAT_SIZE ((unsigned)sizeof(struct ir0_user_stat))

void ir0_stat_to_user(const stat_t *kst, struct ir0_user_stat *ust);

int ir0_copy_stat_to_user(void *user_buf, const stat_t *kst);
