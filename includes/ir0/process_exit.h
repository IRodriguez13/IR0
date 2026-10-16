/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: process_exit.h
 * Description: Product-owned exit cleanup hooks (Linux do_exit owners).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

typedef struct process process_t;

/*
 * Linux kernel/exit.c do_exit() releases files, mm, notify and waiters
 * through subsystem owners. IR0 keeps that split: common process_exit()
 * always reparents, publishes the zombie and deschedules. Optional
 * owners below are skipped when a product image has not bound them.
 */
struct process_exit_services
{
	void (*exit_robust_list)(process_t *dying);
	void (*clear_child_tid)(process_t *dying);
	void (*purge_console_waiters)(process_t *dying);
	void (*purge_pipe_waiters)(process_t *dying);
	void (*purge_ipc_waiters)(process_t *dying);
	void (*clear_console_session)(process_t *dying);
	void (*disarm_clock_wait)(process_t *dying);
	void (*tcp_on_exit)(process_t *dying);
	int (*notify_signal)(int pid, int sig);
};

void process_exit_bind_services(const struct process_exit_services *ops);
const struct process_exit_services *process_exit_services(void);
void process_exit_bind_product(void);
