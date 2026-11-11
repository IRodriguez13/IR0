/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: delay.h
 * Description: Bounded hardware delay (Linux <linux/delay.h> udelay).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

/*
 * udelay - Keep the CPU for about @us microseconds.
 *
 * For short hardware timing (a reset pulse, a register settle time) where
 * nothing can be waited on. It never sleeps and never waits for an event:
 * waiting for an event is a wait queue, waiting for time is a clock sleep.
 * Each ISA implements it with its own time base.
 */
void udelay(unsigned long us);
