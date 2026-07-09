/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: musl_cred_abi.h
 * Description: musl/Linux ABI layout constants for host contract tests
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>

#define IR0_SIGSET_SIZE          128
#define IR0_SIGACTION_MIN_SIZE   152
