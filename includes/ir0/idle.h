/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: idle.h
 * Description: The idle task (Linux do_idle) and its poll registry.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

/*
 * Register one poll for kernel_idle_loop. The list holds 8 pointers.
 * NULL, a pointer already present, and a registration when the list is
 * full are ignored: no allocation and no panic.
 */
void kernel_idle_poll_register(void (*poll)(void));

/*
 * The idle task body (Linux do_idle): runs registered polls, runs woken
 * tasks, and halts the CPU until the next interrupt. Never returns.
 * Implemented in sched/idle.c.
 */
void kernel_idle_loop(void);
