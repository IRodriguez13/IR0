/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: block_fake.h
 * Description: KTM optional in-kernel fake block device for scenario tests.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

#define KTM_BLOCK_FAKE_NAME "ktmfake0"

int ktm_block_fake_setup(void);
void ktm_block_fake_teardown(void);
void ktm_block_fake_reset_reads(void);
unsigned ktm_block_fake_read_count(void);
