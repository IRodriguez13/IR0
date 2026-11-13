/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: entry.h
 * Description: Common work on the way back to userspace from an interrupt
 *              (Linux kernel/entry/common.c).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

/*
 * irqentry_exit_to_user_mode - Exit work for an interrupt that came from
 * userspace (Linux irqentry_exit_to_user_mode()). @frame is the ISA
 * exception frame the interrupt will return through.
 *
 * Called by the ISA after the interrupt is acknowledged and the IRQ nesting
 * is left. A pending reschedule sleeps in schedule() on this kernel stack
 * (preemption of a task running in userspace). A deliverable signal is
 * handled like the syscall exit does: default actions run, or the task
 * returns into its handler with @frame's state saved for rt_sigreturn. In
 * that case the return goes through the task's full user state and this
 * function does not return.
 */
void irqentry_exit_to_user_mode(void *frame);
