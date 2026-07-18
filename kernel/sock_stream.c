/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: sock_stream.c
 * Description: AF_UNIX pathname + TCP loopback + guest-net (10.0.2.x) stream MVP.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/sock_stream.h>
#include <ir0/types.h>
#include <ir0/errno.h>
#include <ir0/kmem.h>
#include <ir0/ktm/fault.h>
#include <config.h>
#include <string.h>

#if CONFIG_ENABLE_NETWORKING
#include "tcp.h"
#endif

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
	uint8_t wire_tcp;
	uint8_t _wire_pad;
	uint16_t wire_local_port;
	uint32_t wire_peer_ip;
	uint16_t wire_peer_port;
	uint32_t wire_seq;
	uint32_t wire_ack;
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

	if (KTM_FAULT_HIT("sock.create"))
		return NULL;

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
#if CONFIG_ENABLE_NETWORKING
	if (s->family == IR0_AF_INET && s->state == SS_LISTEN && s->port != 0)
		tcp_wire_listen_unregister(s->port);
	if (s->wire_tcp)
	{
		uint32_t ack = tcp_wire_peer_ack((ip4_addr_t)s->wire_peer_ip,
						 s->wire_peer_port,
						 s->wire_local_port);
		if (ack)
			s->wire_ack = ack;
		tcp_wire_close((ip4_addr_t)s->wire_peer_ip, s->wire_peer_port,
			       s->wire_local_port, s->wire_seq, s->wire_ack);
	}
#endif
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
#if CONFIG_ENABLE_NETWORKING
	if (s->family == IR0_AF_INET && s->port != 0)
	{
		int ret = tcp_wire_listen_register(s->port);

		if (ret < 0)
		{
			s->state = SS_BOUND;
			return ret;
		}
	}
#endif
	return 0;
}

int sock_stream_socketpair(struct sock_stream **a_out, struct sock_stream **b_out)
{
	struct sock_stream *a;
	struct sock_stream *b;

	if (!a_out || !b_out)
		return -EINVAL;

	a = sock_stream_create(IR0_AF_UNIX);
	if (!a)
		return -ENOMEM;
	b = sock_stream_create(IR0_AF_UNIX);
	if (!b)
	{
		sock_stream_release(a);
		return -ENOMEM;
	}

	a->state = SS_CONNECTED;
	b->state = SS_CONNECTED;
	a->peer = b;
	b->peer = a;
	*a_out = a;
	*b_out = b;
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
	if (child && child->state == SS_CONNECTED)
	{
		s->peer = NULL;
		return child;
	}
#if CONFIG_ENABLE_NETWORKING
	if (s->family == IR0_AF_INET && s->port != 0)
	{
		ip4_addr_t peer_ip;
		uint16_t peer_port;
		uint16_t local_port;
		uint32_t seq;
		uint32_t ack;
		int ret;

		net_stack_poll();
		ret = tcp_wire_accept_take(s->port, &peer_ip, &peer_port,
					   &local_port, &seq, &ack);
		if (ret < 0)
			return NULL;
		child = sock_stream_create(IR0_AF_INET);
		if (!child)
			return NULL;
		child->state = SS_CONNECTED;
		child->wire_tcp = 1;
		child->wire_peer_ip = (uint32_t)peer_ip;
		child->wire_peer_port = peer_port;
		child->wire_local_port = local_port;
		child->wire_seq = seq;
		child->wire_ack = ack;
		child->port = local_port;
		child->listener = s;
		return child;
	}
#endif
	return NULL;
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

static int sock_stream_inet_addr_allowed(uint32_t addr)
{
	uint8_t o0;
	uint8_t o1;
	uint8_t o2;

	if (addr == 0 || addr == 0x0100007fu || addr == 0x7f000001u)
		return 1;

	o0 = (uint8_t)(addr & 0xffu);
	o1 = (uint8_t)((addr >> 8) & 0xffu);
	o2 = (uint8_t)((addr >> 16) & 0xffu);
	if (o0 == 10 && o1 == 0 && o2 == 2)
		return 1;

	o0 = (uint8_t)((addr >> 24) & 0xffu);
	o1 = (uint8_t)((addr >> 16) & 0xffu);
	o2 = (uint8_t)((addr >> 8) & 0xffu);
	if (o0 == 10 && o1 == 0 && o2 == 2)
		return 1;

	return 0;
}

static int sock_stream_is_local_listener(uint16_t port)
{
	int i;

	for (i = 0; i < SS_MAX; i++)
	{
		if (g_socks[i].in_use && g_socks[i].family == IR0_AF_INET &&
		    g_socks[i].state == SS_LISTEN && g_socks[i].port == port)
			return 1;
	}
	return 0;
}

int sock_stream_connect_inet(struct sock_stream *s, uint32_t addr, uint16_t port)
{
	int i;
	struct sock_stream *lst = NULL;
	struct sock_stream *acc;

	if (!s)
		return -EINVAL;
	if (!sock_stream_inet_addr_allowed(addr))
		return -ECONNREFUSED;

	if (!sock_stream_is_local_listener(port))
	{
#if CONFIG_ENABLE_NETWORKING
		uint16_t lport;
		uint32_t seq;
		uint32_t ack;
		int ret;

		ret = tcp_wire_connect((ip4_addr_t)addr, port, &lport, &seq, &ack);
		if (ret < 0)
			return ret;
		s->wire_tcp = 1;
		s->wire_local_port = lport;
		s->wire_peer_ip = addr;
		s->wire_peer_port = port;
		s->wire_seq = seq;
		s->wire_ack = ack;
		s->port = lport;
		s->state = SS_CONNECTED;
		s->peer = NULL;
		return 0;
#else
		return -ECONNREFUSED;
#endif
	}

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

#if CONFIG_ENABLE_NETWORKING
	if (s->wire_tcp)
	{
		uint32_t ack = tcp_wire_peer_ack((ip4_addr_t)s->wire_peer_ip,
						 s->wire_peer_port,
						 s->wire_local_port);
		if (ack)
			s->wire_ack = ack;
		int ret = tcp_wire_send((ip4_addr_t)s->wire_peer_ip, s->wire_peer_port,
					s->wire_local_port, &s->wire_seq, s->wire_ack,
					buf, len);
		return (ret < 0) ? ret : (ssize_t)ret;
	}
#endif

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

#if CONFIG_ENABLE_NETWORKING
	if (s->wire_tcp)
	{
		int ret;
		uint32_t ack;

		net_stack_poll();
		ack = tcp_wire_peer_ack((ip4_addr_t)s->wire_peer_ip,
					s->wire_peer_port, s->wire_local_port);
		if (ack)
			s->wire_ack = ack;
		ret = tcp_wire_recv((ip4_addr_t)s->wire_peer_ip, s->wire_peer_port,
				   s->wire_local_port, buf, len);
		if (ret < 0)
			return ret;
		return (ssize_t)ret;
	}
#endif

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
