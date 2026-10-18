/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: early_anonfd.h
 * Description: Staged pipe/eventfd/memfd/timerfd/epoll object tables
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#include <ir0/early_rootfs.h>

#define EARLY_PIPE_MAX 2
#define EARLY_PIPE_BUF 1024
#define EARLY_EFD_MAX 2
#define EARLY_MEMFD_MAX 2
#define EARLY_MEMFD_CAP 4096
#define EARLY_TIMERFD_MAX 2
#define EARLY_EPOLL_MAX 2
#define EARLY_EPOLL_SLOTS 8

struct early_pipe
{
	int used;
	unsigned rpos;
	unsigned len;
	int readers;
	int writers;
	uint8_t buf[EARLY_PIPE_BUF];
};

struct early_eventfd
{
	int used;
	uint64_t count;
	int nonblock;
	int semaphore;
};

struct early_memfd
{
	int used;
	int refs;
	uint32_t len;
	uint32_t mode;
	uint8_t data[EARLY_MEMFD_CAP];
};

struct early_timerfd
{
	int used;
	int refs;
	int clock;
	int armed;
	uint64_t expire;
	uint64_t interval;
	uint64_t ticks;
};

struct early_epoll
{
	int used;
	int refs;
	struct
	{
		int used;
		int fd;
		uint32_t events;
		uint64_t data;
	} slot[EARLY_EPOLL_SLOTS];
};

extern struct early_pipe g_early_pipes[EARLY_PIPE_MAX];
extern struct early_eventfd g_early_eventfd[EARLY_EFD_MAX];
extern struct early_memfd g_early_memfd[EARLY_MEMFD_MAX];
extern struct early_timerfd g_early_timerfd[EARLY_TIMERFD_MAX];
extern struct early_epoll g_early_epoll[EARLY_EPOLL_MAX];

void early_anonfd_reset(void);
unsigned early_pipe_consume(struct early_pipe *p, uint8_t *tmp, unsigned n);
unsigned early_pipe_peek(struct early_pipe *p, uint8_t *tmp, unsigned n);
unsigned early_pipe_deposit(struct early_pipe *p, const uint8_t *tmp,
			    unsigned n);
