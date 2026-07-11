/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: sock_stream.c
 * Description: AF_UNIX pathname + TCP 127.0.0.1 loopback stream MVP.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/sock_stream.h>
#include <ir0/types.h>
#include <ir0/errno.h>
#include <ir0/kmem.h>
#include <string.h>

#define SS_BUF 4096
#define SS_MAX 16
#define SS_PATH 108

enum ss_state
{
	SS_IDLE = 0,
	SS_BOUND,
	SS_LISTEN,
	SS_CONNECTED,
};

struct sock_stream
{
	int in_use;
	int family;
	enum ss_state state;
	char path[SS_PATH];
	uint16_t port;
	struct sock_stream *peer;
	struct sock_stream *listener;
	char buf[SS_BUF];
	unsigned head;
	unsigned tail;
	unsigned count;
	uint8_t magic;
};

#define SS_MAGIC 0xA5

static struct sock_stream g_socks[SS_MAX];

int sock_stream_is(const void *ptr)
{
	const struct sock_stream *s = ptr;
	uintptr_t base = (uintptr_t)&g_socks[0];
	uintptr_t end = (uintptr_t)&g_socks[SS_MAX];
	uintptr_t p = (uintptr_t)ptr;

	if (p < base || p >= end)
		return 0;
	if (((p - base) % sizeof(g_socks[0])) != 0)
		return 0;
	return s && s->magic == SS_MAGIC && s->in_use;
}

struct sock_stream *sock_stream_create(int family)
{
	int i;

	for (i = 0; i < SS_MAX; i++)
	{
		if (!g_socks[i].in_use)
		{
			memset(&g_socks[i], 0, sizeof(g_socks[i]));
			g_socks[i].in_use = 1;
			g_socks[i].family = family;
			g_socks[i].magic = SS_MAGIC;
			g_socks[i].state = SS_IDLE;
			return &g_socks[i];
		}
	}
	return NULL;
}

void sock_stream_release(struct sock_stream *s)
{
	if (!s || !s->in_use)
		return;
	if (s->peer && s->peer->peer == s)
		s->peer->peer = NULL;
	memset(s, 0, sizeof(*s));
}

int sock_stream_bind_unix(struct sock_stream *s, const char *path)
{
	int i;

	if (!s || !path || path[0] == '\0')
		return -EINVAL;
	if (strlen(path) >= SS_PATH)
		return -ENAMETOOLONG;
	for (i = 0; i < SS_MAX; i++)
	{
		if (g_socks[i].in_use && g_socks[i].family == IR0_AF_UNIX &&
		    g_socks[i].state != SS_IDLE &&
		    strcmp(g_socks[i].path, path) == 0)
			return -EADDRINUSE;
	}
	strncpy(s->path, path, SS_PATH - 1);
	s->state = SS_BOUND;
	return 0;
}

int sock_stream_listen(struct sock_stream *s, int backlog)
{
	(void)backlog;
	if (!s || s->state != SS_BOUND)
		return -EINVAL;
	s->state = SS_LISTEN;
	return 0;
}

int sock_stream_connect_unix(struct sock_stream *s, const char *path)
{
	int i;
	struct sock_stream *lst = NULL;
	struct sock_stream *acc;

	if (!s || !path)
		return -EINVAL;
	for (i = 0; i < SS_MAX; i++)
	{
		if (g_socks[i].in_use && g_socks[i].state == SS_LISTEN &&
		    g_socks[i].family == IR0_AF_UNIX &&
		    strcmp(g_socks[i].path, path) == 0)
		{
			lst = &g_socks[i];
			break;
		}
	}
	if (!lst)
		return -ECONNREFUSED;
	acc = sock_stream_create(IR0_AF_UNIX);
	if (!acc)
		return -ENOMEM;
	acc->state = SS_CONNECTED;
	acc->peer = s;
	s->state = SS_CONNECTED;
	s->peer = acc;
	s->listener = lst;
	lst->peer = acc; /* pending accept picks this up */
	return 0;
}

struct sock_stream *sock_stream_accept(struct sock_stream *s)
{
	struct sock_stream *child;

	if (!s || s->state != SS_LISTEN)
		return NULL;
	child = s->peer;
	if (!child || child->state != SS_CONNECTED)
		return NULL;
	s->peer = NULL;
	return child;
}

int sock_stream_bind_inet(struct sock_stream *s, uint16_t port)
{
	int i;

	if (!s)
		return -EINVAL;
	for (i = 0; i < SS_MAX; i++)
	{
		if (g_socks[i].in_use && g_socks[i].family == IR0_AF_INET &&
		    g_socks[i].state != SS_IDLE && g_socks[i].port == port)
			return -EADDRINUSE;
	}
	s->port = port;
	s->state = SS_BOUND;
	return 0;
}

int sock_stream_connect_inet(struct sock_stream *s, uint32_t addr, uint16_t port)
{
	int i;
	struct sock_stream *lst = NULL;
	struct sock_stream *acc;

	if (!s)
		return -EINVAL;
	/* Loopback only (127.0.0.1 network order 0x0100007f or host 0x7f000001). */
	if (addr != 0x0100007fu && addr != 0x7f000001u && addr != 0)
		return -ECONNREFUSED;
	for (i = 0; i < SS_MAX; i++)
	{
		if (g_socks[i].in_use && g_socks[i].family == IR0_AF_INET &&
		    g_socks[i].state == SS_LISTEN && g_socks[i].port == port)
		{
			lst = &g_socks[i];
			break;
		}
	}
	if (!lst)
		return -ECONNREFUSED;
	acc = sock_stream_create(IR0_AF_INET);
	if (!acc)
		return -ENOMEM;
	acc->state = SS_CONNECTED;
	acc->peer = s;
	acc->port = port;
	s->state = SS_CONNECTED;
	s->peer = acc;
	lst->peer = acc;
	return 0;
}

ssize_t sock_stream_send(struct sock_stream *s, const void *buf, size_t len)
{
	struct sock_stream *peer;
	size_t i;
	const char *src = buf;

	if (!s || s->state != SS_CONNECTED || !buf)
		return -EINVAL;
	peer = s->peer;
	if (!peer)
		return -EPIPE;
	for (i = 0; i < len; i++)
	{
		if (peer->count >= SS_BUF)
			break;
		peer->buf[peer->head] = src[i];
		peer->head = (peer->head + 1) % SS_BUF;
		peer->count++;
	}
	return (ssize_t)i;
}

ssize_t sock_stream_recv(struct sock_stream *s, void *buf, size_t len)
{
	size_t i;
	char *dst = buf;

	if (!s || s->state != SS_CONNECTED || !buf)
		return -EINVAL;
	for (i = 0; i < len; i++)
	{
		if (s->count == 0)
			break;
		dst[i] = s->buf[s->tail];
		s->tail = (s->tail + 1) % SS_BUF;
		s->count--;
	}
	return (ssize_t)i;
}
