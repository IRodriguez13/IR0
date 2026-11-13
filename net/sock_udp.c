/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: sock_udp.c
 * Description: UDP socket objects (RX queue + bind/ephemeral ports)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/sock_udp.h>
#include <ir0/sock_stream.h>
#include <ir0/sock_icmp.h>
#include <ir0/kmem.h>
#include <ir0/errno.h>
#include <ir0/clock.h>
#include <ir0/net.h>
#include <ir0/arch_port.h>
#include <config.h>
#include <string.h>
#include "udp.h"
#include <ir0/spinlock.h>

#define SOCK_UDP_RX_MAX     32
#define SOCK_UDP_PKT_MAX    1472
#define SOCK_UDP_RECV_MS    30000
#define MSG_DONTWAIT        0x40
#define SOCK_UDP_LOOPBACK_NET  127U
#define SOCK_UDP_LOOPBACK_ADDR 0x7F000001U /* 127.0.0.1, host order */

struct sock_udp_rx_pkt
{
	struct sock_udp_rx_pkt *next;
	ip4_addr_t src_ip;
	uint16_t src_port;
	uint16_t len;
	uint8_t data[SOCK_UDP_PKT_MAX];
};

struct sock_udp
{
	int refcount;
	uint16_t local_port;
	bool bound;
	bool connected;
	uint32_t peer_ip_be;
	uint16_t peer_port;
	struct sock_udp *bound_next;
	struct sock_udp_rx_pkt *rx_head;
	struct sock_udp_rx_pkt *rx_tail;
	size_t rx_count;
};

static struct sock_udp *sock_bound_list;
#if CONFIG_ENABLE_NETWORKING
static uint16_t sock_next_ephemeral = 32768;
#endif

static ir0_spinlock_t sock_udp_lock;

#if CONFIG_ENABLE_NETWORKING
static bool sock_port_in_use(uint16_t port)
{
	struct sock_udp *s;

	for (s = sock_bound_list; s; s = s->bound_next)
	{
		if (s->bound && s->local_port == port)
			return true;
	}
	return false;
}

static uint16_t sock_pick_ephemeral(void)
{
	uint16_t start = sock_next_ephemeral;
	uint16_t port = start;

	do
	{
		if (port >= 32768 && port <= 60999 && !sock_port_in_use(port))
		{
			sock_next_ephemeral = (uint16_t)(port + 1);
			if (sock_next_ephemeral < 32768)
				sock_next_ephemeral = 32768;
			return port;
		}
		port++;
		if (port > 60999)
			port = 32768;
	} while (port != start);

	return 0;
}

static void sock_udp_rx_enqueue(struct sock_udp *sock, ip4_addr_t src_ip,
				uint16_t src_port, const void *data, size_t len)
{
	struct sock_udp_rx_pkt *pkt;
	uint64_t flags;

	/* len == 0 is an empty datagram, queued like any other. */
	if (!sock || !data || len > SOCK_UDP_PKT_MAX)
		return;

	pkt = kmalloc(sizeof(*pkt));
	if (!pkt)
		return;

	pkt->src_ip = src_ip;
	pkt->src_port = src_port;
	pkt->len = (uint16_t)len;
	memcpy(pkt->data, data, len);
	pkt->next = NULL;

	flags = ir0_spin_lock_irqsave(&sock_udp_lock);
	while (sock->rx_count >= SOCK_UDP_RX_MAX && sock->rx_head)
	{
		struct sock_udp_rx_pkt *drop = sock->rx_head;

		sock->rx_head = drop->next;
		if (!sock->rx_head)
			sock->rx_tail = NULL;
		sock->rx_count--;
		kfree(drop);
	}
	if (!sock->rx_tail)
	{
		sock->rx_head = pkt;
		sock->rx_tail = pkt;
	}
	else
	{
		sock->rx_tail->next = pkt;
		sock->rx_tail = pkt;
	}
	sock->rx_count++;
	ir0_spin_unlock_irqrestore(&sock_udp_lock, flags);
}

static void sock_udp_rx_from_net(struct net_device *dev, ip4_addr_t src_ip,
				 uint16_t src_port, const void *data, size_t len,
				 void *priv)
{
	struct sock_udp *owner = (struct sock_udp *)priv;

	(void)dev;
	if (!owner)
		return;
	sock_udp_rx_enqueue(owner, src_ip, src_port, data, len);
}

/*
 * Loopback (Linux 127.0.0.0/8 via lo): queue the datagram on the socket
 * bound to @dest_port, as if it had arrived from 127.0.0.1:@src_port.
 * With no socket on that port the datagram is dropped; an unconnected
 * sender sees no error (the ICMP port-unreachable reaches only connected
 * sockets on Linux).
 */
static void sock_udp_loopback(uint16_t src_port, uint16_t dest_port,
			      const void *data, size_t len)
{
	struct sock_udp *dst;
	uint64_t flags;

	flags = ir0_spin_lock_irqsave(&sock_udp_lock);
	for (dst = sock_bound_list; dst; dst = dst->bound_next)
	{
		if (dst->bound && dst->local_port == dest_port)
			break;
	}
	if (dst)
		dst->refcount++;
	ir0_spin_unlock_irqrestore(&sock_udp_lock, flags);
	if (!dst)
		return;

	sock_udp_rx_enqueue(dst, htonl(SOCK_UDP_LOOPBACK_ADDR), src_port, data,
			    len);
	sock_udp_release(dst);
}

static int sock_udp_install_port_handler(struct sock_udp *sock, uint16_t port)
{
	sock->local_port = port;
	sock->bound = true;
	sock->bound_next = sock_bound_list;
	sock_bound_list = sock;
	udp_register_handler(port, sock_udp_rx_from_net, sock);
	return 0;
}

#endif /* CONFIG_ENABLE_NETWORKING */

struct sock_udp *sock_udp_create(void)
{
#if !CONFIG_ENABLE_NETWORKING
	return NULL;
#else
	struct sock_udp *sock;

	sock = kmalloc(sizeof(*sock));
	if (!sock)
		return NULL;
	memset(sock, 0, sizeof(*sock));
	sock->refcount = 1;
	return sock;
#endif
}

void sock_udp_acquire(struct sock_udp *sock)
{
	uint64_t flags;

	if (!sock)
		return;
	flags = ir0_spin_lock_irqsave(&sock_udp_lock);
	sock->refcount++;
	ir0_spin_unlock_irqrestore(&sock_udp_lock, flags);
}

static void sock_udp_free_rx(struct sock_udp *sock)
{
	struct sock_udp_rx_pkt *pkt;

	while (sock->rx_head)
	{
		pkt = sock->rx_head;
		sock->rx_head = pkt->next;
		kfree(pkt);
	}
	sock->rx_tail = NULL;
	sock->rx_count = 0;
}

static void sock_udp_unbind_locked(struct sock_udp *sock)
{
#if CONFIG_ENABLE_NETWORKING
	struct sock_udp **pp;

	if (!sock->bound)
		return;

	udp_unregister_handler(sock->local_port);
	for (pp = &sock_bound_list; *pp; pp = &(*pp)->bound_next)
	{
		if (*pp == sock)
		{
			*pp = sock->bound_next;
			break;
		}
	}
	sock->bound = false;
	sock->bound_next = NULL;
	sock->local_port = 0;
#else
	(void)sock;
#endif
}

void sock_udp_release(struct sock_udp *sock)
{
	int refs;
	uint64_t flags;

	if (!sock)
		return;
	/*
	 * AF_UNIX/TCP stream sockets live in a static table. After the last
	 * stream release clears magic, a duplicate close used to fall through
	 * here and kfree() the slot → "pointer out of heap range" panic.
	 */
	if (sock_stream_is_slot(sock))
		return;
	if (sock_icmp_is(sock))
		return;

	flags = ir0_spin_lock_irqsave(&sock_udp_lock);
	refs = --sock->refcount;
	if (refs > 0)
	{
		ir0_spin_unlock_irqrestore(&sock_udp_lock, flags);
		return;
	}

	sock_udp_unbind_locked(sock);
	ir0_spin_unlock_irqrestore(&sock_udp_lock, flags);

	sock_udp_free_rx(sock);
	kfree(sock);
}

int sock_udp_bind(struct sock_udp *sock, uint16_t port_host)
{
#if !CONFIG_ENABLE_NETWORKING
	(void)sock;
	(void)port_host;
	return -ENOSYS;
#else
	uint64_t flags;
	int ret;

	if (!sock)
		return -EINVAL;
	if (port_host == 0)
		return -EINVAL;
	if (sock_port_in_use(port_host))
		return -EADDRINUSE;

	flags = ir0_spin_lock_irqsave(&sock_udp_lock);
	if (sock->bound)
	{
		ir0_spin_unlock_irqrestore(&sock_udp_lock, flags);
		return -EINVAL;
	}
	ret = sock_udp_install_port_handler(sock, port_host);
	ir0_spin_unlock_irqrestore(&sock_udp_lock, flags);
	return ret;
#endif
}

#if CONFIG_ENABLE_NETWORKING
static int sock_udp_ensure_port(struct sock_udp *sock)
{
	uint16_t port;
	int ret;
	uint64_t flags;

	if (!sock)
		return -EINVAL;
	if (sock->bound)
		return 0;

	port = sock_pick_ephemeral();
	if (port == 0)
		return -EADDRINUSE;

	flags = ir0_spin_lock_irqsave(&sock_udp_lock);
	if (sock->bound)
	{
		ir0_spin_unlock_irqrestore(&sock_udp_lock, flags);
		return 0;
	}
	ret = sock_udp_install_port_handler(sock, port);
	ir0_spin_unlock_irqrestore(&sock_udp_lock, flags);
	return ret;
}
#endif

int sock_udp_connect(struct sock_udp *sock, uint32_t peer_ip_be,
		     uint16_t peer_port_host)
{
#if !CONFIG_ENABLE_NETWORKING
	(void)sock;
	(void)peer_ip_be;
	(void)peer_port_host;
	return -ENOSYS;
#else
	uint64_t flags;

	if (!sock || peer_port_host == 0)
		return -EINVAL;

	flags = ir0_spin_lock_irqsave(&sock_udp_lock);
	sock->peer_ip_be = peer_ip_be;
	sock->peer_port = peer_port_host;
	sock->connected = true;
	ir0_spin_unlock_irqrestore(&sock_udp_lock, flags);
	return 0;
#endif
}

int sock_udp_peer(struct sock_udp *sock, uint32_t *peer_ip_be,
		  uint16_t *peer_port_host)
{
	uint64_t flags;
	int ret = -EDESTADDRREQ;

	if (!sock)
		return -EINVAL;
	flags = ir0_spin_lock_irqsave(&sock_udp_lock);
	if (sock->connected)
	{
		*peer_ip_be = sock->peer_ip_be;
		*peer_port_host = sock->peer_port;
		ret = 0;
	}
	ir0_spin_unlock_irqrestore(&sock_udp_lock, flags);
	return ret;
}

int sock_udp_sendto(struct sock_udp *sock, uint32_t dest_ip_be, uint16_t dest_port_host,
		    const void *data, size_t len)
{
#if !CONFIG_ENABLE_NETWORKING
	(void)sock;
	(void)dest_ip_be;
	(void)dest_port_host;
	(void)data;
	(void)len;
	return -ENOSYS;
#else
	struct net_device *dev;
	ip4_addr_t dest_ip = dest_ip_be;
	int ret;

	/* A zero length sends an empty datagram (Linux). */
	if (!sock || !data)
		return -EINVAL;
	if (len > SOCK_UDP_PKT_MAX)
		return -EMSGSIZE;

	ret = sock_udp_ensure_port(sock);
	if (ret < 0)
		return ret;

	if ((ntohl(dest_ip) >> 24) == SOCK_UDP_LOOPBACK_NET)
	{
		sock_udp_loopback(sock->local_port, dest_port_host, data, len);
		return (int)len;
	}

	dev = net_get_devices();
	if (!dev)
		return -ENETUNREACH;

	ret = udp_send(dev, dest_ip, sock->local_port, dest_port_host, data, len);
	if (ret < 0)
		return -EIO;
	return (int)len;
#endif
}

ssize_t sock_udp_recvfrom(struct sock_udp *sock, void *buf, size_t len, int flags,
			  uint32_t *src_ip_be_out, uint16_t *src_port_out)
{
#if !CONFIG_ENABLE_NETWORKING
	(void)sock;
	(void)buf;
	(void)len;
	(void)flags;
	(void)src_ip_be_out;
	(void)src_port_out;
	return -ENOSYS;
#else
	struct sock_udp_rx_pkt *pkt = NULL;
	uint64_t flags_irq;
	uint64_t start;
	bool nonblock = (flags & MSG_DONTWAIT) != 0;
	size_t copy_len;

	/* A zero length still dequeues the next datagram (truncated). */
	if (!sock || !buf)
		return -EINVAL;

	start = clock_get_uptime_milliseconds();
	for (;;)
	{
		flags_irq = ir0_spin_lock_irqsave(&sock_udp_lock);
		if (sock->rx_head)
		{
			pkt = sock->rx_head;
			sock->rx_head = pkt->next;
			if (!sock->rx_head)
				sock->rx_tail = NULL;
			sock->rx_count--;
		}
		ir0_spin_unlock_irqrestore(&sock_udp_lock, flags_irq);

		if (pkt)
			break;

		if (nonblock)
			return -EAGAIN;

		if (clock_get_uptime_milliseconds() - start >= SOCK_UDP_RECV_MS)
			return -EAGAIN;

		net_stack_poll();
	}

	copy_len = pkt->len;
	if (copy_len > len)
		copy_len = len;
	memcpy(buf, pkt->data, copy_len);
	if (src_ip_be_out)
		*src_ip_be_out = (uint32_t)pkt->src_ip;
	if (src_port_out)
		*src_port_out = pkt->src_port;
	kfree(pkt);
	return (ssize_t)copy_len;
#endif
}

int sock_udp_walk(int (*cb)(const struct sock_udp_snap *s, void *ctx), void *ctx)
{
	struct sock_udp *s;
	struct sock_udp_snap snap;
	uint64_t flags;

	if (!cb)
		return -EINVAL;

	flags = ir0_spin_lock_irqsave(&sock_udp_lock);
	for (s = sock_bound_list; s; s = s->bound_next)
	{
		if (!s->bound)
			continue;
		memset(&snap, 0, sizeof(snap));
#if CONFIG_ENABLE_NETWORKING
		snap.local_ip = (uint32_t)ip_local_addr;
#else
		snap.local_ip = 0;
#endif
		snap.local_port = s->local_port;
		if (s->connected)
		{
			snap.rem_ip = s->peer_ip_be;
			snap.rem_port = s->peer_port;
			snap.st = 0x01;
		}
		else
			snap.st = 0x07;
		snap.inode = (unsigned long)(uintptr_t)s;
		if (cb(&snap, ctx) != 0)
		{
			ir0_spin_unlock_irqrestore(&sock_udp_lock, flags);
			return -1;
		}
	}
	ir0_spin_unlock_irqrestore(&sock_udp_lock, flags);
	return 0;
}
