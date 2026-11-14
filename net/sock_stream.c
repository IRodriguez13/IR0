/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: sock_stream.c
 * Description: AF_UNIX stream and datagram sockets (pathname and abstract
 *              names), TCP loopback and guest-net (10.0.2.x) streams.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/sock_stream.h>
#include <ir0/socket.h>
#include <ir0/types.h>
#include <ir0/errno.h>
#include <ir0/kmem.h>
#include <ir0/copy_user.h>
#include <ir0/ktm/fault.h>
#include <ir0/arch_cpu.h>
#include <ir0/logging.h>
#include <config.h>
#include <string.h>
#include <ir0/poll.h>

#if CONFIG_ENABLE_NETWORKING
#include "tcp.h"
#include <ir0/net.h>
#endif

/* X11 setup/property bursts exceed one page.  Linux AF_UNIX sockets provide
 * substantially larger queues; a page-sized queue can deadlock peers when a
 * blocking writev must complete before the reader is scheduled. */
#define SS_CHUNK_SIZE 4096U
#define SS_QUEUE_MAX  (1024U * 1024U)
#define SS_PATH 108
/*
 * AF_UNIX datagrams (net/unix/af_unix.c): one message may not exceed the
 * default send buffer less its overhead (EMSGSIZE), and a receiver queues
 * at most SS_DGRAM_QLEN of them before senders wait (EAGAIN).
 */
#define SS_DGRAM_MAX  (212992U - 32U)
#define SS_DGRAM_QLEN 64U

struct ss_chunk {
	struct ss_chunk *next;
	unsigned start;
	unsigned end;
	char data[SS_CHUNK_SIZE];
};

/* One queued datagram and the address of the socket that sent it. */
struct ss_dgram
{
	struct ss_dgram *next;
	size_t len;
	size_t from_len;
	char from[SS_PATH];
	char data[];
};

enum ss_state
{
	SS_IDLE = 0,
	SS_BOUND,
	SS_LISTEN,
	SS_CONNECTING,
	SS_CONNECTED,
};

struct sock_stream
{
	int in_use;
	int fd_refs; /* close/fork: free slot only when last fd drops */
	int family;
	int type; /* IR0_SOCK_STREAM or IR0_SOCK_DGRAM */
	enum ss_state state;
	struct sock_unix_addr addr; /* key_len 0: unbound */
	uint16_t port;
	struct sock_stream *peer;
	struct sock_stream *listener;
	struct sock_stream *accept_head;
	struct sock_stream *accept_tail;
	struct sock_stream *accept_next;
	unsigned accept_count;
	unsigned accept_backlog;
	struct ss_chunk *rx_head;
	struct ss_chunk *rx_tail;
	unsigned queued_bytes;
	struct ss_dgram *dg_head; /* IR0_SOCK_DGRAM receive queue */
	struct ss_dgram *dg_tail;
	unsigned dg_count;
	uint8_t peer_gone; /* datagram peer closed: next send ECONNREFUSED */
	uint8_t wire_tcp;
	uint8_t shut_rd;
	uint8_t shut_wr;
	uint8_t reuseaddr;
	uint16_t wire_local_port;
	uint32_t wire_peer_ip;
	uint16_t wire_peer_port;
	uint32_t wire_seq;
	uint32_t wire_ack;
	int so_error; /* positive errno for SO_ERROR */
	uint64_t rcv_timeout_ms; /* 0 = block forever */
	uint64_t snd_timeout_ms;
	uint8_t magic;
	uint8_t rights_n;
	uint8_t rights[SOCK_STREAM_RIGHTS_MAX][SOCK_STREAM_RIGHTS_ENTRY_SIZE];
};

#define SS_MAGIC 0xA5

static struct sock_stream g_socks[CONFIG_STREAM_SOCKET_CAPACITY];
static void (*g_rights_dtor)(void *entry, size_t sz);

void sock_stream_set_rights_dtor(void (*dtor)(void *entry, size_t sz))
{
	g_rights_dtor = dtor;
}

static void sock_stream_rights_clear(struct sock_stream *s)
{
	int i;

	if (!s)
		return;
	for (i = 0; i < s->rights_n; i++)
	{
		if (g_rights_dtor)
			g_rights_dtor(s->rights[i], SOCK_STREAM_RIGHTS_ENTRY_SIZE);
	}
	s->rights_n = 0;
}

int sock_stream_rights_push_batch(struct sock_stream *recv_side,
				  const void *entries, size_t count,
				  size_t entry_sz)
{
	const uint8_t *src = (const uint8_t *)entries;
	size_t i;

	if (!recv_side || (!entries && count != 0) ||
	    entry_sz == 0 || entry_sz > SOCK_STREAM_RIGHTS_ENTRY_SIZE)
		return -EINVAL;
	if (count > (size_t)(SOCK_STREAM_RIGHTS_MAX - recv_side->rights_n))
		return -ENOBUFS;
	for (i = 0; i < count; i++)
	{
		uint8_t *dst = recv_side->rights[recv_side->rights_n + i];

		memset(dst, 0, SOCK_STREAM_RIGHTS_ENTRY_SIZE);
		memcpy(dst, src + i * entry_sz, entry_sz);
	}
	recv_side->rights_n += (uint8_t)count;
	if (count != 0)
		poll_wake_check();
	return 0;
}

int sock_stream_rights_pop(struct sock_stream *s, void *entry, size_t sz)
{
	if (!s || !entry || sz == 0 || sz > SOCK_STREAM_RIGHTS_ENTRY_SIZE)
		return -EINVAL;
	if (s->rights_n == 0)
		return -EAGAIN;
	memcpy(entry, s->rights[0], sz);
	s->rights_n--;
	if (s->rights_n > 0)
	{
		memmove(s->rights[0], s->rights[1],
			(size_t)s->rights_n * SOCK_STREAM_RIGHTS_ENTRY_SIZE);
	}
	return 0;
}

int sock_stream_rights_count(const struct sock_stream *s)
{
	return s ? (int)s->rights_n : 0;
}

int sock_stream_family(const struct sock_stream *s)
{
	return s ? s->family : 0;
}

int sock_stream_type(const struct sock_stream *s)
{
	return s ? s->type : 0;
}

int sock_stream_buf_count(const struct sock_stream *s)
{
	return s ? (int)s->queued_bytes : 0;
}

int sock_stream_buf_space(const struct sock_stream *peer_of_sender)
{
	if (!peer_of_sender)
		return 0;
	return (int)(SS_QUEUE_MAX - peer_of_sender->queued_bytes);
}

struct sock_stream *sock_stream_get_peer(struct sock_stream *s)
{
	return s ? s->peer : NULL;
}

static int sock_stream_wire_progress(struct sock_stream *s)
{
#if !CONFIG_ENABLE_NETWORKING
	(void)s;
	return 0;
#else
	uint32_t seq;
	uint32_t ack;
	int ret;

	if (!s || !s->wire_tcp || s->state != SS_CONNECTING)
		return 0;

	ret = tcp_wire_connect_poll((ip4_addr_t)s->wire_peer_ip, s->wire_peer_port,
				    s->wire_local_port, &seq, &ack);
	if (ret == -EAGAIN)
		return 0;
	if (ret < 0)
	{
		s->so_error = -ret;
		s->state = SS_BOUND;
		poll_wake_check();
		return ret;
	}
	s->wire_seq = seq;
	s->wire_ack = ack;
	s->state = SS_CONNECTED;
	s->so_error = 0;
	poll_wake_check();
	return 1;
#endif
}

int sock_stream_poll_readable(const struct sock_stream *s)
{
	struct sock_stream *mut = (struct sock_stream *)s;

	if (!s)
		return 0;
	if (s->type == IR0_SOCK_DGRAM)
		return s->dg_head || s->shut_rd;
	if (s->state == SS_LISTEN && s->accept_head)
		return 1;
	if (s->state == SS_CONNECTING)
	{
		(void)sock_stream_wire_progress(mut);
		return 0;
	}
	if (s->state != SS_CONNECTED)
		return 0;
	if (s->rights_n > 0)
		return 1;
	if (s->queued_bytes > 0)
		return 1;
	if (s->shut_rd)
		return 1;
#if CONFIG_ENABLE_NETWORKING
	if (s->wire_tcp)
		return tcp_wire_poll_readable((ip4_addr_t)s->wire_peer_ip,
					      s->wire_peer_port,
					      s->wire_local_port);
#endif
	if (!s->peer || s->peer->shut_wr)
		return 1;
	return 0;
}

int sock_stream_poll_writable(const struct sock_stream *s)
{
	struct sock_stream *mut = (struct sock_stream *)s;

	if (!s)
		return 0;
	if (s->type == IR0_SOCK_DGRAM)
		return !s->shut_wr &&
		       (!s->peer || s->peer->dg_count < SS_DGRAM_QLEN);
	if (s->state == SS_CONNECTING)
	{
		(void)sock_stream_wire_progress(mut);
		/* Linux: POLLOUT when connect finished (ok or error). */
		if (s->state == SS_CONNECTED || s->so_error)
			return 1;
		return 0;
	}
	if (s->state != SS_CONNECTED)
		return 0;
	if (s->so_error)
		return 1;
	if (s->shut_wr)
		return 0;
#if CONFIG_ENABLE_NETWORKING
	if (s->wire_tcp)
		return tcp_wire_poll_writable((ip4_addr_t)s->wire_peer_ip,
					      s->wire_peer_port,
					      s->wire_local_port);
#endif
	if (!s->peer)
		return 0;
	if (s->peer->shut_rd)
		return 0;
	return s->peer->queued_bytes < SS_QUEUE_MAX;
}

static void sock_stream_queue_free(struct ss_chunk *chunk)
{
	while (chunk) {
		struct ss_chunk *next = chunk->next;
		kfree(chunk);
		chunk = next;
	}
}

static size_t sock_stream_queue_write(struct sock_stream *s,
				      const char *src, size_t len)
{
	size_t written = 0;

	while (written < len) {
		struct ss_chunk *chunk;
		unsigned long flags;
		size_t room, take;

		flags = irq_save();
		if (s->queued_bytes >= SS_QUEUE_MAX) {
			irq_restore(flags);
			break;
		}
		chunk = s->rx_tail;
		if (chunk && chunk->end < SS_CHUNK_SIZE) {
			room = SS_CHUNK_SIZE - chunk->end;
			take = len - written;
			if (take > room) take = room;
			if (take > SS_QUEUE_MAX - s->queued_bytes)
				take = SS_QUEUE_MAX - s->queued_bytes;
			memcpy(chunk->data + chunk->end, src + written, take);
			chunk->end += (unsigned)take;
			s->queued_bytes += (unsigned)take;
			irq_restore(flags);
			written += take;
			continue;
		}
		irq_restore(flags);

		chunk = kmalloc(sizeof(*chunk));
		if (!chunk) break;
		memset(chunk, 0, sizeof(*chunk));
		flags = irq_save();
		if (s->rx_tail && s->rx_tail->end < SS_CHUNK_SIZE) {
			irq_restore(flags);
			kfree(chunk);
			continue;
		}
		if (s->queued_bytes >= SS_QUEUE_MAX) {
			irq_restore(flags);
			kfree(chunk);
			break;
		}
		if (s->rx_tail) s->rx_tail->next = chunk;
		else s->rx_head = chunk;
		s->rx_tail = chunk;
		irq_restore(flags);
	}
	return written;
}

/*
 * Dequeue up to @len bytes into @dst, a kernel buffer, or a user buffer
 * when @to_user is set. Like Linux unix_stream_read_generic(), a byte
 * leaves the queue only once it reached @dst: a user copy that faults
 * stops there, and *@fault reports it.
 */
static size_t sock_stream_queue_read(struct sock_stream *s, char *dst,
				     size_t len, int peek, int to_user,
				     int *fault)
{
	struct ss_chunk *chunk, *free_head = NULL, *free_tail = NULL;
	unsigned long flags;
	size_t copied = 0;

	*fault = 0;
	flags = irq_save();
	chunk = s->rx_head;
	while (chunk && copied < len) {
		size_t available = chunk->end - chunk->start;
		size_t take = len - copied;
		if (take > available) take = available;
		if (!to_user)
			memcpy(dst + copied, chunk->data + chunk->start, take);
		else if (copy_to_user(dst + copied, chunk->data + chunk->start,
				      take) != 0) {
			*fault = 1;
			break;
		}
		copied += take;
		if (peek) {
			chunk = chunk->next;
			continue;
		}
		chunk->start += (unsigned)take;
		s->queued_bytes -= (unsigned)take;
		if (chunk->start != chunk->end) break;
		s->rx_head = chunk->next;
		if (!s->rx_head) s->rx_tail = NULL;
		chunk->next = NULL;
		if (free_tail) free_tail->next = chunk;
		else free_head = chunk;
		free_tail = chunk;
		chunk = s->rx_head;
	}
	irq_restore(flags);
	sock_stream_queue_free(free_head);
	return copied;
}

int sock_stream_is(const void *ptr)
{
	const struct sock_stream *s = ptr;
	uintptr_t base = (uintptr_t)&g_socks[0];
	uintptr_t end = (uintptr_t)&g_socks[CONFIG_STREAM_SOCKET_CAPACITY];
	uintptr_t p = (uintptr_t)ptr;

	if (p < base || p >= end)
		return 0;
	if (((p - base) % sizeof(g_socks[0])) != 0)
		return 0;
	return s && s->magic == SS_MAGIC && s->in_use;
}

int sock_stream_is_slot(const void *ptr)
{
	uintptr_t base = (uintptr_t)&g_socks[0];
	uintptr_t end = (uintptr_t)&g_socks[CONFIG_STREAM_SOCKET_CAPACITY];
	uintptr_t p = (uintptr_t)ptr;

	if (p < base || p >= end)
		return 0;
	if (((p - base) % sizeof(g_socks[0])) != 0)
		return 0;
	return 1;
}

void sock_stream_acquire(struct sock_stream *s)
{
	unsigned long irq_flags;

	if (!s || !s->in_use || s->magic != SS_MAGIC)
		return;

	irq_flags = irq_save();
	s->fd_refs++;
	irq_restore(irq_flags);
}

struct sock_stream *sock_stream_create(int family, int type)
{
	int i;

	if (KTM_FAULT_HIT("sock.create"))
		return NULL;

	for (i = 0; i < CONFIG_STREAM_SOCKET_CAPACITY; i++)
	{
		if (!g_socks[i].in_use)
		{
			memset(&g_socks[i], 0, sizeof(g_socks[i]));
			g_socks[i].in_use = 1;
			g_socks[i].fd_refs = 1;
			g_socks[i].family = family;
			g_socks[i].type = type;
			g_socks[i].magic = SS_MAGIC;
			g_socks[i].state = SS_IDLE;
			return &g_socks[i];
		}
	}
	log_warn("SOCKET", "stream socket table exhausted");
	return NULL;
}

/*
 * Drop the receive queue of a datagram socket that is going away, and
 * disconnect the datagram sockets connected to it: their next send is
 * ECONNREFUSED, as when Linux finds the peer SOCK_DEAD.
 */
static void sock_stream_dgram_release(struct sock_stream *s)
{
	struct ss_dgram *dg;
	unsigned long irq_flags;
	int i;

	irq_flags = irq_save();
	dg = s->dg_head;
	s->dg_head = NULL;
	s->dg_tail = NULL;
	s->dg_count = 0;
	for (i = 0; i < CONFIG_STREAM_SOCKET_CAPACITY; i++)
	{
		if (g_socks[i].in_use && g_socks[i].peer == s &&
		    g_socks[i].type == IR0_SOCK_DGRAM)
		{
			g_socks[i].peer = NULL;
			g_socks[i].peer_gone = 1;
		}
	}
	irq_restore(irq_flags);
	while (dg)
	{
		struct ss_dgram *next = dg->next;

		kfree(dg);
		dg = next;
	}
}

void sock_stream_release(struct sock_stream *s)
{
	unsigned long irq_flags;
	struct sock_stream *queued;
	struct ss_chunk *rx_queue;

	if (!s || !s->in_use)
		return;

	irq_flags = irq_save();
	if (s->fd_refs > 1)
	{
		s->fd_refs--;
		irq_restore(irq_flags);
		return;
	}
	s->fd_refs = 0;
	irq_restore(irq_flags);
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
	/* Detach the whole accept queue before releasing its endpoints. */
	irq_flags = irq_save();
	queued = s->accept_head;
	s->accept_head = NULL;
	s->accept_tail = NULL;
	s->accept_count = 0;
	irq_restore(irq_flags);
	while (queued)
	{
		struct sock_stream *next = queued->accept_next;

		queued->accept_next = NULL;
		sock_stream_release(queued);
		queued = next;
	}
	if (s->type == IR0_SOCK_DGRAM)
		sock_stream_dgram_release(s);
	if (s->peer && s->peer->peer == s)
		s->peer->peer = NULL;
	sock_stream_rights_clear(s);
	rx_queue = s->rx_head;
	s->rx_head = NULL;
	s->rx_tail = NULL;
	s->queued_bytes = 0;
	sock_stream_queue_free(rx_queue);
	memset(s, 0, sizeof(*s));
	poll_wake_check();
}

static int unix_name_equal(const struct sock_stream *a,
			   const struct sock_unix_addr *addr)
{
	if (!a || a->family != IR0_AF_UNIX)
		return 0;
	if (a->addr.is_abstract != addr->is_abstract)
		return 0;
	if (a->addr.key_len != addr->key_len)
		return 0;
	return memcmp(a->addr.key, addr->key, addr->key_len) == 0;
}

static int unix_addr_valid(const struct sock_unix_addr *addr)
{
	if (!addr || addr->key_len == 0 || addr->key_len >= SS_PATH)
		return 0;
	return addr->is_abstract || addr->key[0] != '\0';
}

int sock_stream_bind_unix(struct sock_stream *s, const struct sock_unix_addr *addr)
{
	int i;

	if (!s || !unix_addr_valid(addr))
		return -EINVAL;
	for (i = 0; i < CONFIG_STREAM_SOCKET_CAPACITY; i++)
	{
		if (g_socks[i].in_use && g_socks[i].family == IR0_AF_UNIX &&
		    g_socks[i].state != SS_IDLE &&
		    unix_name_equal(&g_socks[i], addr))
		{
			/*
			 * SO_REUSEADDR MVP: steal name from a peer that is only
			 * BOUND (not LISTEN/CONNECTED).
			 */
			if (s->reuseaddr && g_socks[i].state == SS_BOUND &&
			    &g_socks[i] != s)
			{
				memset(&g_socks[i].addr, 0, sizeof(g_socks[i].addr));
				continue;
			}
			return -EADDRINUSE;
		}
	}
	s->addr = *addr;
	s->addr.key[addr->key_len] = '\0';
	s->state = SS_BOUND;
	return 0;
}

const struct sock_unix_addr *sock_stream_unix_addr(const struct sock_stream *s)
{
	if (!s || s->family != IR0_AF_UNIX || s->addr.key_len == 0)
		return NULL;
	return &s->addr;
}

int sock_stream_shutdown(struct sock_stream *s, int how)
{
	if (!s || s->state != SS_CONNECTED)
		return -ENOTCONN;
	/* SHUT_RD=0 SHUT_WR=1 SHUT_RDWR=2 */
	if (how < 0 || how > 2)
		return -EINVAL;
	if (how == 0 || how == 2)
		s->shut_rd = 1;
	if (how == 1 || how == 2)
		s->shut_wr = 1;
	poll_wake_check();
	return 0;
}

int sock_stream_listen(struct sock_stream *s, int backlog)
{
	if (!s || s->state != SS_BOUND)
		return -EINVAL;
	if (backlog < 1)
		backlog = 1;
	if (backlog > CONFIG_STREAM_SOCKET_CAPACITY)
		backlog = CONFIG_STREAM_SOCKET_CAPACITY;
	s->accept_backlog = (unsigned)backlog;
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

static int sock_stream_accept_enqueue(struct sock_stream *listener,
				      struct sock_stream *child)
{
	unsigned long irq_flags;

	if (!listener || !child || listener->state != SS_LISTEN)
		return -EINVAL;
	irq_flags = irq_save();
	if (listener->accept_count >= listener->accept_backlog)
	{
		irq_restore(irq_flags);
		return -ECONNREFUSED;
	}
	child->accept_next = NULL;
	if (listener->accept_tail)
		listener->accept_tail->accept_next = child;
	else
		listener->accept_head = child;
	listener->accept_tail = child;
	listener->accept_count++;
	irq_restore(irq_flags);
	return 0;
}

int sock_stream_socketpair(int type, struct sock_stream **a_out,
			   struct sock_stream **b_out)
{
	struct sock_stream *a;
	struct sock_stream *b;

	if (!a_out || !b_out)
		return -EINVAL;

	a = sock_stream_create(IR0_AF_UNIX, type);
	if (!a)
		return -ENOMEM;
	b = sock_stream_create(IR0_AF_UNIX, type);
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

/* The socket that owns @addr (bound or listening), or NULL. */
static struct sock_stream *unix_find_bound(const struct sock_unix_addr *addr)
{
	int i;

	for (i = 0; i < CONFIG_STREAM_SOCKET_CAPACITY; i++)
	{
		struct sock_stream *o = &g_socks[i];

		if (o->in_use && (o->state == SS_BOUND || o->state == SS_LISTEN) &&
		    unix_name_equal(o, addr))
			return o;
	}
	return NULL;
}

/*
 * unix_find_other() for a datagram target: the owner must exist
 * (ECONNREFUSED) and be a datagram socket (EPROTOTYPE).
 */
static struct sock_stream *unix_find_dgram(const struct sock_unix_addr *addr,
					   int *err)
{
	struct sock_stream *o = unix_find_bound(addr);

	*err = 0;
	if (!o)
		*err = -ECONNREFUSED;
	else if (o->type != IR0_SOCK_DGRAM)
		*err = -EPROTOTYPE;
	return *err ? NULL : o;
}

int sock_stream_connect_unix(struct sock_stream *s,
			     const struct sock_unix_addr *addr)
{
	int i;
	struct sock_stream *lst = NULL;
	struct sock_stream *acc;
	enum ss_state old_state;

	if (!s || !unix_addr_valid(addr))
		return -EINVAL;
	if (s->type == IR0_SOCK_DGRAM)
	{
		/* unix_dgram_connect(): only this side records the peer. */
		int err;
		struct sock_stream *other = unix_find_dgram(addr, &err);

		if (!other)
			return err;
		s->peer = other;
		s->peer_gone = 0;
		return 0;
	}
	if (s->state == SS_CONNECTED)
		return -EISCONN;
	if (s->state != SS_IDLE && s->state != SS_BOUND)
		return -EINVAL;
	lst = unix_find_bound(addr);
	if (lst && lst->type != s->type)
		return -EPROTOTYPE;
	lst = NULL;
	old_state = s->state;
	for (i = 0; i < CONFIG_STREAM_SOCKET_CAPACITY; i++)
	{
		if (g_socks[i].in_use && g_socks[i].state == SS_LISTEN &&
		    unix_name_equal(&g_socks[i], addr))
		{
			lst = &g_socks[i];
			break;
		}
	}
	if (!lst)
	{
		return -ECONNREFUSED;
	}
	acc = sock_stream_create(IR0_AF_UNIX, IR0_SOCK_STREAM);
	if (!acc)
		return -ENOMEM;
	/* Accepted endpoint inherits listener bind name (Linux getsockname/peer). */
	acc->addr = lst->addr;
	acc->state = SS_CONNECTED;
	acc->peer = s;
	s->state = SS_CONNECTED;
	s->peer = acc;
	s->listener = lst;
	if (sock_stream_accept_enqueue(lst, acc) != 0)
	{
		s->peer = NULL;
		s->listener = NULL;
		s->state = old_state;
		acc->peer = NULL;
		sock_stream_release(acc);
		return -ECONNREFUSED;
	}
	poll_wake_check();
	return 0;
}

struct sock_stream *sock_stream_accept(struct sock_stream *s)
{
	struct sock_stream *child;
	unsigned long irq_flags;

	if (!s || s->state != SS_LISTEN)
		return NULL;
	irq_flags = irq_save();
	child = s->accept_head;
	if (child)
	{
		s->accept_head = child->accept_next;
		if (!s->accept_head)
			s->accept_tail = NULL;
		child->accept_next = NULL;
		if (s->accept_count > 0)
			s->accept_count--;
		irq_restore(irq_flags);
		return child;
	}
	irq_restore(irq_flags);
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
		child = sock_stream_create(IR0_AF_INET, IR0_SOCK_STREAM);
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
	for (i = 0; i < CONFIG_STREAM_SOCKET_CAPACITY; i++)
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

	for (i = 0; i < CONFIG_STREAM_SOCKET_CAPACITY; i++)
	{
		if (g_socks[i].in_use && g_socks[i].family == IR0_AF_INET &&
		    g_socks[i].state == SS_LISTEN && g_socks[i].port == port)
			return 1;
	}
	return 0;
}

int sock_stream_connect_inet_flags(struct sock_stream *s, uint32_t addr,
				   uint16_t port, int nonblock)
{
	int i;
	struct sock_stream *lst = NULL;
	struct sock_stream *acc;
	enum ss_state old_state;

	if (!s)
		return -EINVAL;
	if (!sock_stream_inet_addr_allowed(addr))
		return -ECONNREFUSED;
	if (s->state == SS_CONNECTING)
	{
		int pr = sock_stream_wire_progress(s);

		if (s->state == SS_CONNECTED)
			return 0;
		if (s->so_error)
			return -s->so_error;
		if (pr == 0)
			return -EALREADY;
		return pr < 0 ? pr : 0;
	}
	if (s->state == SS_CONNECTED)
		return -EISCONN;
	if (s->state != SS_IDLE && s->state != SS_BOUND)
		return -EINVAL;
	old_state = s->state;

	if (!sock_stream_is_local_listener(port))
	{
#if CONFIG_ENABLE_NETWORKING
		uint16_t lport;
		uint32_t seq;
		uint32_t ack;
		int ret;

		if (nonblock)
		{
			ret = tcp_wire_connect_start((ip4_addr_t)addr, port, &lport);
			if (ret < 0)
				return ret;
			s->wire_tcp = 1;
			s->wire_local_port = lport;
			s->wire_peer_ip = addr;
			s->wire_peer_port = port;
			s->port = lport;
			s->peer = NULL;
			s->so_error = 0;
			s->state = SS_CONNECTING;
			return -EINPROGRESS;
		}

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
		s->so_error = 0;
		return 0;
#else
		(void)nonblock;
		return -ECONNREFUSED;
#endif
	}

	for (i = 0; i < CONFIG_STREAM_SOCKET_CAPACITY; i++)
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
	acc = sock_stream_create(IR0_AF_INET, IR0_SOCK_STREAM);
	if (!acc)
		return -ENOMEM;
	acc->state = SS_CONNECTED;
	acc->peer = s;
	acc->port = port;
	s->state = SS_CONNECTED;
	s->peer = acc;
	if (sock_stream_accept_enqueue(lst, acc) != 0)
	{
		s->peer = NULL;
		s->state = old_state;
		acc->peer = NULL;
		sock_stream_release(acc);
		return -ECONNREFUSED;
	}
	poll_wake_check();
	return 0;
}

ssize_t sock_stream_send(struct sock_stream *s, const void *buf, size_t len)
{
	struct sock_stream *peer;
	size_t i;
	const char *src = buf;

	if (s && s->type == IR0_SOCK_DGRAM)
		return sock_stream_sendto(s, NULL, buf, len);
	if (!s || (s->state != SS_CONNECTED && s->state != SS_CONNECTING) || !buf)
		return -EINVAL;

#if CONFIG_ENABLE_NETWORKING
	if (s->wire_tcp && s->state == SS_CONNECTING)
	{
		int pr = sock_stream_wire_progress(s);

		if (s->so_error)
			return -s->so_error;
		if (s->state != SS_CONNECTED)
			return pr < 0 ? pr : -EAGAIN;
	}
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

	if (s->state != SS_CONNECTED)
		return -EINVAL;
	if (s->shut_wr)
		return -EPIPE;
	peer = s->peer;
	if (!peer)
		return -EPIPE;
	if (peer->shut_rd)
		return -EPIPE;
	i = sock_stream_queue_write(peer, src, len);
	if (i > 0)
		poll_wake_check();
	return (ssize_t)i;
}

ssize_t sock_stream_sendto(struct sock_stream *s,
			   const struct sock_unix_addr *dest,
			   const void *buf, size_t len)
{
	struct sock_stream *other;
	struct ss_dgram *dg;
	unsigned long irq_flags;
	int err;

	if (!s || (len && !buf))
		return -EINVAL;
	if (s->type != IR0_SOCK_DGRAM)
		return sock_stream_send(s, buf, len);
	if (s->shut_wr)
		return -EPIPE;
	if (len > SS_DGRAM_MAX)
		return -EMSGSIZE;
	if (dest)
	{
		other = unix_find_dgram(dest, &err);
		if (!other)
			return err;
		/* unix_may_send(): a receiver connected elsewhere refuses us. */
		if (other->peer && other->peer != s)
			return -EPERM;
	}
	else
	{
		other = s->peer;
		if (!other)
		{
			if (!s->peer_gone)
				return -ENOTCONN;
			s->peer_gone = 0;
			return -ECONNREFUSED;
		}
	}
	if (other->shut_rd)
		return -EPIPE;
	dg = kmalloc(sizeof(*dg) + (len ? len : 1));
	if (!dg)
		return -ENOBUFS;
	memset(dg, 0, sizeof(*dg));
	dg->len = len;
	if (s->addr.key_len)
	{
		memcpy(dg->from, s->addr.sun_path, sizeof(dg->from));
		dg->from_len = s->addr.sun_len;
	}
	if (len)
		memcpy(dg->data, buf, len);
	irq_flags = irq_save();
	if (!other->in_use || other->dg_count >= SS_DGRAM_QLEN ||
	    other->queued_bytes + len > SS_QUEUE_MAX)
	{
		int full = other->in_use;

		irq_restore(irq_flags);
		kfree(dg);
		return full ? -EAGAIN : -ECONNREFUSED;
	}
	if (other->dg_tail)
		other->dg_tail->next = dg;
	else
		other->dg_head = dg;
	other->dg_tail = dg;
	other->dg_count++;
	other->queued_bytes += (unsigned)len;
	irq_restore(irq_flags);
	poll_wake_check();
	return (ssize_t)len;
}

/*
 * unix_dgram_recvmsg(): take the whole next datagram. A short @len
 * truncates it (the rest is lost); MSG_TRUNC reports its full length and
 * MSG_PEEK leaves it queued. A user copy that faults keeps it queued.
 */
static ssize_t sock_stream_dgram_recv(struct sock_stream *s, char *dst,
				      size_t len, int flags, int to_user,
				      struct sock_unix_addr *from)
{
	struct ss_dgram *dg;
	unsigned long irq_flags;
	size_t n;

	irq_flags = irq_save();
	dg = s->dg_head;
	if (!dg)
	{
		irq_restore(irq_flags);
		return s->shut_rd ? 0 : -EAGAIN;
	}
	n = len < dg->len ? len : dg->len;
	if (n && to_user && copy_to_user(dst, dg->data, n) != 0)
	{
		irq_restore(irq_flags);
		return -EFAULT;
	}
	if (n && !to_user)
		memcpy(dst, dg->data, n);
	if (from)
	{
		memset(from, 0, sizeof(*from));
		memcpy(from->sun_path, dg->from, sizeof(dg->from));
		from->sun_len = dg->from_len;
	}
	if (flags & MSG_TRUNC)
		n = dg->len;
	if (flags & MSG_PEEK)
	{
		irq_restore(irq_flags);
		return (ssize_t)n;
	}
	s->dg_head = dg->next;
	if (!s->dg_head)
		s->dg_tail = NULL;
	s->dg_count--;
	s->queued_bytes -= (unsigned)dg->len;
	irq_restore(irq_flags);
	kfree(dg);
	poll_wake_check();
	return (ssize_t)n;
}

/* Bounce size for a user read from the wire TCP backend. */
#define SS_WIRE_USER_BOUNCE 512

static ssize_t sock_stream_recv_common(struct sock_stream *s, void *buf,
				       size_t len, int flags, int to_user,
				       struct sock_unix_addr *from)
{
	size_t i;
	char *dst = buf;
	int peek = (flags & MSG_PEEK) != 0;
	int fault;

	if (s && s->type == IR0_SOCK_DGRAM)
		return sock_stream_dgram_recv(s, dst, len, flags, to_user, from);
	/* A user buffer is checked by the copy; a zero length needs none. */
	if (!s || (s->state != SS_CONNECTED && s->state != SS_CONNECTING) ||
	    (!buf && !to_user))
		return -EINVAL;

#if CONFIG_ENABLE_NETWORKING
	if (s->wire_tcp && s->state == SS_CONNECTING)
	{
		(void)sock_stream_wire_progress(s);
		if (s->so_error)
			return -s->so_error;
		if (s->state != SS_CONNECTED)
			return -EAGAIN;
	}
	if (s->wire_tcp)
	{
		char bounce[SS_WIRE_USER_BOUNCE];
		int ret;
		uint32_t ack;

		(void)peek;
		net_stack_poll();
		ack = tcp_wire_peer_ack((ip4_addr_t)s->wire_peer_ip,
					s->wire_peer_port, s->wire_local_port);
		if (ack)
			s->wire_ack = ack;
		/*
		 * The wire backend dequeues as it copies and has no peek: a
		 * user read goes through a bounce buffer, and a faulting user
		 * copy loses those bytes.
		 */
		if (to_user && len > sizeof(bounce))
			len = sizeof(bounce);
		ret = tcp_wire_recv((ip4_addr_t)s->wire_peer_ip, s->wire_peer_port,
				   s->wire_local_port, to_user ? bounce : buf, len);
		if (ret < 0)
			return ret;
		if (to_user && ret > 0 && copy_to_user(buf, bounce, (size_t)ret) != 0)
			return -EFAULT;
		return (ssize_t)ret;
	}
#endif

	if (s->shut_rd)
		return 0;
	/* Linux copies nothing and reports 0 once data is queued. */
	if (len == 0 && s->queued_bytes > 0)
		return 0;
	i = sock_stream_queue_read(s, dst, len, peek, to_user, &fault);
	if (i == 0 && fault)
		return -EFAULT;
	if (i == 0 && (!s->peer || s->peer->shut_wr))
		return 0;
	if (i == 0)
		return -EAGAIN; /* caller may block; MSG_DONTWAIT keeps -EAGAIN */
	if (i > 0)
		poll_wake_check();
	return (ssize_t)i;
}

ssize_t sock_stream_recv_flags(struct sock_stream *s, void *buf, size_t len, int flags,
			       struct sock_unix_addr *from)
{
	return sock_stream_recv_common(s, buf, len, flags, 0, from);
}

ssize_t sock_stream_recv_user(struct sock_stream *s, void *ubuf, size_t len,
			      int flags, struct sock_unix_addr *from)
{
	return sock_stream_recv_common(s, ubuf, len, flags, 1, from);
}

ssize_t sock_stream_recv(struct sock_stream *s, void *buf, size_t len)
{
	return sock_stream_recv_flags(s, buf, len, 0, NULL);
}

int sock_stream_set_reuseaddr(struct sock_stream *s, int on)
{
	if (!s)
		return -EINVAL;
	s->reuseaddr = on ? 1 : 0;
	return 0;
}

int sock_stream_get_reuseaddr(const struct sock_stream *s)
{
	return s ? (int)s->reuseaddr : 0;
}

int sock_stream_take_so_error(struct sock_stream *s)
{
	int err;

	if (!s)
		return 0;
	if (s->state == SS_CONNECTING)
		(void)sock_stream_wire_progress(s);
	err = s->so_error;
	s->so_error = 0;
	return err;
}

int sock_stream_set_timeout_ms(struct sock_stream *s, int is_rcv, uint64_t ms)
{
	if (!s)
		return -EINVAL;
	if (is_rcv)
		s->rcv_timeout_ms = ms;
	else
		s->snd_timeout_ms = ms;
	return 0;
}

uint64_t sock_stream_get_timeout_ms(const struct sock_stream *s, int is_rcv)
{
	if (!s)
		return 0;
	return is_rcv ? s->rcv_timeout_ms : s->snd_timeout_ms;
}

static uint8_t sock_stream_linux_st(enum ss_state st)
{
	switch (st)
	{
	case SS_CONNECTED:
		return 0x01; /* TCP_ESTABLISHED */
	case SS_CONNECTING:
		return 0x02; /* TCP_SYN_SENT */
	case SS_LISTEN:
		return 0x0A; /* TCP_LISTEN */
	case SS_BOUND:
		return 0x07; /* TCP_CLOSE */
	default:
		return 0x07;
	}
}

int sock_stream_inet_walk(int (*cb)(const struct sock_stream_inet_snap *s,
				    void *ctx),
			  void *ctx)
{
	int i;
	struct sock_stream_inet_snap snap;

	if (!cb)
		return -EINVAL;

	for (i = 0; i < CONFIG_STREAM_SOCKET_CAPACITY; i++)
	{
		struct sock_stream *s = &g_socks[i];

		if (!s->in_use || s->magic != SS_MAGIC)
			continue;
		if (s->family != IR0_AF_INET || s->state == SS_IDLE)
			continue;

		memset(&snap, 0, sizeof(snap));
#if CONFIG_ENABLE_NETWORKING
		{
			struct ip_config cfg;

			ip_get_config(&cfg);
			snap.local_ip = (uint32_t)cfg.addr;
		}
#else
		snap.local_ip = 0;
#endif
	snap.local_port = s->wire_local_port ? s->wire_local_port : s->port;
		if (s->state == SS_CONNECTED || s->state == SS_CONNECTING)
		{
			snap.rem_ip = s->wire_peer_ip;
			snap.rem_port = s->wire_peer_port;
		}
		snap.st = sock_stream_linux_st(s->state);
#if CONFIG_ENABLE_NETWORKING
		if (s->wire_tcp && s->state == SS_CONNECTED &&
		    tcp_wire_peer_fin((ip4_addr_t)s->wire_peer_ip,
				     s->wire_peer_port, s->wire_local_port))
			snap.st = 0x08; /* TCP_CLOSE_WAIT */
#endif
		snap.inode = (unsigned long)(uintptr_t)s;
		if (cb(&snap, ctx) != 0)
			return -1;
	}
	return 0;
}

int sock_stream_unix_walk(int (*cb)(const struct sock_stream_unix_snap *s,
				    void *ctx),
			  void *ctx)
{
	int i;
	struct sock_stream_unix_snap snap;

	if (!cb)
		return -EINVAL;

	for (i = 0; i < CONFIG_STREAM_SOCKET_CAPACITY; i++)
	{
		struct sock_stream *s = &g_socks[i];

		if (!s->in_use || s->magic != SS_MAGIC)
			continue;
		if (s->family != IR0_AF_UNIX || s->state == SS_IDLE)
			continue;

		memset(&snap, 0, sizeof(snap));
		snap.inode = (unsigned long)(uintptr_t)s;
		snap.refcnt = (unsigned)(s->fd_refs > 0 ? s->fd_refs : 1);
		snap.type = (uint8_t)s->type;
		snap.st = sock_stream_linux_st(s->state);
		snap.path_len = s->addr.key_len;
		snap.is_abstract = s->addr.is_abstract;
		if (s->addr.key_len < sizeof(snap.path))
			memcpy(snap.path, s->addr.key, s->addr.key_len);
		if (cb(&snap, ctx) != 0)
			return -1;
	}
	return 0;
}
