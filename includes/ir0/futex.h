/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: futex.h
 * Description: futex wait/wake facade
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

struct process;

int ir0_futex_wait(struct process *proc, int *uaddr, int val);
int ir0_futex_wake(int *uaddr, int count);
void ir0_futex_drop_process(struct process *proc);
