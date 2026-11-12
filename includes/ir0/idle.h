/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: idle.h
 * Description: The idle task (Linux do_idle).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

/*
 * The idle task body (Linux do_idle): runs woken tasks and halts the CPU
 * until the next interrupt. Never returns. Implemented in sched/idle.c.
 */
void kernel_idle_loop(void);
