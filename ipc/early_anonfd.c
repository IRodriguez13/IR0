/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: early_anonfd.c
 * Description: Staged pipe/eventfd/memfd/timerfd/epoll tables (ipc/)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/early_anonfd.h>

struct early_pipe g_early_pipes[EARLY_PIPE_MAX];
struct early_eventfd g_early_eventfd[EARLY_EFD_MAX];
struct early_memfd g_early_memfd[EARLY_MEMFD_MAX];
struct early_timerfd g_early_timerfd[EARLY_TIMERFD_MAX];
struct early_epoll g_early_epoll[EARLY_EPOLL_MAX];

void early_anonfd_reset(void)
{
	unsigned i;

	for (i = 0; i < EARLY_PIPE_MAX; i++)
		g_early_pipes[i].used = 0;
	for (i = 0; i < EARLY_EFD_MAX; i++)
		g_early_eventfd[i].used = 0;
	for (i = 0; i < EARLY_MEMFD_MAX; i++)
	{
		g_early_memfd[i].used = 0;
		g_early_memfd[i].mode = IR0_EARLY_ROOTFS_MODE_REG;
	}
	for (i = 0; i < EARLY_TIMERFD_MAX; i++)
		g_early_timerfd[i].used = 0;
	for (i = 0; i < EARLY_EPOLL_MAX; i++)
		g_early_epoll[i].used = 0;
}

unsigned early_pipe_consume(struct early_pipe *p, uint8_t *tmp, unsigned n)
{
	unsigned i;

	if (!p || !tmp)
		return 0;
	if (n > p->len)
		n = p->len;
	for (i = 0; i < n; i++)
	{
		tmp[i] = p->buf[p->rpos];
		p->rpos = (p->rpos + 1U) % EARLY_PIPE_BUF;
	}
	p->len -= n;
	return n;
}

unsigned early_pipe_peek(struct early_pipe *p, uint8_t *tmp, unsigned n)
{
	unsigned i;
	unsigned rpos;

	if (!p || !tmp)
		return 0;
	if (n > p->len)
		n = p->len;
	rpos = p->rpos;
	for (i = 0; i < n; i++)
	{
		tmp[i] = p->buf[rpos];
		rpos = (rpos + 1U) % EARLY_PIPE_BUF;
	}
	return n;
}

unsigned early_pipe_deposit(struct early_pipe *p, const uint8_t *tmp,
			    unsigned n)
{
	unsigned i;
	unsigned wpos;
	unsigned room;

	if (!p || !tmp)
		return 0;
	room = EARLY_PIPE_BUF - p->len;
	if (n > room)
		n = room;
	wpos = (p->rpos + p->len) % EARLY_PIPE_BUF;
	for (i = 0; i < n; i++)
	{
		p->buf[wpos] = tmp[i];
		wpos = (wpos + 1U) % EARLY_PIPE_BUF;
	}
	p->len += n;
	return n;
}
