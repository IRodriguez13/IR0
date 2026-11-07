/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: usercopy_early.h
 * Description: Staged EL0 user buffers — MMU range check then byte copy.
 */

#pragma once

#include <stdint.h>

int arm64_early_copy_from_user(void *destination, uint64_t source,
			       uint64_t size);
int arm64_early_copy_to_user(uint64_t destination, const void *source,
			     uint64_t size);
void arm64_early_zero_user_page(uint64_t page);
