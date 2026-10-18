/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: early_unix.c
 * Description: Staged AF_UNIX / socketpair tables (ipc/)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/early_unix.h>

struct early_sockpair g_early_sock[EARLY_SOCKPAIR_MAX];
struct early_unix g_early_unix[EARLY_UNIX_MAX];

void early_unix_reset(void)
{
	unsigned i;

	for (i = 0; i < EARLY_SOCKPAIR_MAX; i++)
		g_early_sock[i].used = 0;
	for (i = 0; i < EARLY_UNIX_MAX; i++)
		g_early_unix[i].used = 0;
}

unsigned early_unix_consume(struct early_unix *u, uint8_t *tmp, unsigned n)
{
	if (!u)
		return 0;
	return early_ipc_ring_consume(&u->rpos, &u->len, u->buf, tmp, n);
}

unsigned early_unix_peek(struct early_unix *u, uint8_t *tmp, unsigned n)
{
	if (!u)
		return 0;
	return early_ipc_ring_peek(u->rpos, u->len, u->buf, tmp, n);
}

unsigned early_unix_deposit(struct early_unix *u, const uint8_t *tmp,
			    unsigned n)
{
	if (!u)
		return 0;
	return early_ipc_ring_deposit(&u->rpos, &u->len, u->buf, tmp, n);
}

unsigned early_sock_consume(struct early_sockpair *s, int dir, uint8_t *tmp,
			    unsigned n)
{
	if (!s || dir < 0 || dir > 1)
		return 0;
	return early_ipc_ring_consume(&s->dir[dir].rpos, &s->dir[dir].len,
				      s->dir[dir].buf, tmp, n);
}

unsigned early_sock_peek(struct early_sockpair *s, int dir, uint8_t *tmp,
			 unsigned n)
{
	if (!s || dir < 0 || dir > 1)
		return 0;
	return early_ipc_ring_peek(s->dir[dir].rpos, s->dir[dir].len,
				   s->dir[dir].buf, tmp, n);
}

unsigned early_sock_deposit(struct early_sockpair *s, int dir,
			    const uint8_t *tmp, unsigned n)
{
	if (!s || dir < 0 || dir > 1)
		return 0;
	return early_ipc_ring_deposit(&s->dir[dir].rpos, &s->dir[dir].len,
				      s->dir[dir].buf, tmp, n);
}
