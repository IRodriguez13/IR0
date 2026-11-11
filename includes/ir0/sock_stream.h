/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: sock_stream.h
 * Description: Minimal AF_UNIX + TCP loopback stream sockets.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/types.h>
#include <stddef.h>
#include <stdint.h>

#define IR0_AF_UNIX 1
#define IR0_AF_INET 2
#define IR0_SOCK_STREAM 1
#define IR0_SOCK_DGRAM 2

#define SOCK_STREAM_RIGHTS_MAX 4
#define SOCK_STREAM_RIGHTS_ENTRY_SIZE 320

struct sock_stream;

#define SOCK_UNIX_PATH_MAX 108

/*
 * An AF_UNIX address. @key finds the socket: the resolved path of a
 * pathname address, or the bytes of an abstract name (leading NUL
 * included). @sun_path and @sun_len are the address as the caller gave
 * it, which getsockname() reports: @sun_len counts the family and, for a
 * pathname, the terminating NUL (Linux unix_mkname_bsd()).
 */
struct sock_unix_addr
{
	char key[SOCK_UNIX_PATH_MAX];
	size_t key_len;
	int is_abstract;
	char sun_path[SOCK_UNIX_PATH_MAX];
	size_t sun_len;
};

/* @type: IR0_SOCK_STREAM, or IR0_SOCK_DGRAM (AF_UNIX only). */
struct sock_stream *sock_stream_create(int family, int type);
void sock_stream_release(struct sock_stream *s);
int sock_stream_bind_unix(struct sock_stream *s, const struct sock_unix_addr *addr);
int sock_stream_listen(struct sock_stream *s, int backlog);
int sock_stream_connect_unix(struct sock_stream *s,
			     const struct sock_unix_addr *addr);
struct sock_stream *sock_stream_accept(struct sock_stream *s);
int sock_stream_bind_inet(struct sock_stream *s, uint16_t port);
int sock_stream_connect_inet(struct sock_stream *s, uint32_t addr, uint16_t port);
int sock_stream_connect_inet_flags(struct sock_stream *s, uint32_t addr, uint16_t port,
				   int nonblock);
ssize_t sock_stream_send(struct sock_stream *s, const void *buf, size_t len);
/*
 * Send one datagram to @dest, or to the connected peer when @dest is NULL
 * (unix_dgram_sendmsg()). -ENOTCONN without a peer, -EAGAIN while the
 * receiver's queue is full. On a stream socket, same as sock_stream_send().
 */
ssize_t sock_stream_sendto(struct sock_stream *s,
			   const struct sock_unix_addr *dest,
			   const void *buf, size_t len);
ssize_t sock_stream_recv(struct sock_stream *s, void *buf, size_t len);
/* Kernel-buffer receive; @from as for sock_stream_recv_user(). */
ssize_t sock_stream_recv_flags(struct sock_stream *s, void *buf, size_t len, int flags,
			       struct sock_unix_addr *from);
/*
 * sock_stream_recv_user - recv into a user buffer for recv/read syscalls.
 * Bytes are dequeued only once copied (Linux unix_stream_read_generic): a
 * bad @ubuf gives -EFAULT with the queue untouched, a fault after some bytes
 * returns that short count. A datagram socket returns one whole datagram
 * and, when @from is not NULL, its sender's address (sun_len 0: unbound).
 */
ssize_t sock_stream_recv_user(struct sock_stream *s, void *ubuf, size_t len,
			      int flags, struct sock_unix_addr *from);
int sock_stream_set_reuseaddr(struct sock_stream *s, int on);
int sock_stream_get_reuseaddr(const struct sock_stream *s);
int sock_stream_take_so_error(struct sock_stream *s);
int sock_stream_set_timeout_ms(struct sock_stream *s, int is_rcv, uint64_t ms);
uint64_t sock_stream_get_timeout_ms(const struct sock_stream *s, int is_rcv);
int sock_stream_is(const void *ptr);
/* Address in g_socks[] even if already released (magic cleared). */
int sock_stream_is_slot(const void *ptr);
void sock_stream_acquire(struct sock_stream *s);
int sock_stream_socketpair(int type, struct sock_stream **a_out,
			   struct sock_stream **b_out);

int sock_stream_rights_push(struct sock_stream *recv_side, const void *entry, size_t sz);
int sock_stream_rights_push_batch(struct sock_stream *recv_side,
				  const void *entries, size_t count,
				  size_t entry_sz);
int sock_stream_rights_pop(struct sock_stream *s, void *entry, size_t sz);
int sock_stream_rights_count(const struct sock_stream *s);
void sock_stream_set_rights_dtor(void (*dtor)(void *entry, size_t sz));

int sock_stream_shutdown(struct sock_stream *s, int how);
/*
 * The address @s is bound to (an accepted socket: its listener's). Returns
 * NULL for a socket that has none, as an unbound or non-AF_UNIX socket.
 */
const struct sock_unix_addr *sock_stream_unix_addr(const struct sock_stream *s);
int sock_stream_poll_readable(const struct sock_stream *s);
int sock_stream_poll_writable(const struct sock_stream *s);
int sock_stream_family(const struct sock_stream *s);
int sock_stream_type(const struct sock_stream *s);
int sock_stream_buf_space(const struct sock_stream *peer_of_sender);
int sock_stream_buf_count(const struct sock_stream *s);
int sock_stream_is_recv_shutdown(const struct sock_stream *s);
struct sock_stream *sock_stream_get_peer(struct sock_stream *s);

/* /proc/net/{tcp,unix} inventory (BusyBox netstat). */
struct sock_stream_inet_snap
{
	uint32_t local_ip;
	uint16_t local_port;
	uint32_t rem_ip;
	uint16_t rem_port;
	uint8_t st; /* Linux tcp_state hex */
	unsigned long inode;
};

struct sock_stream_unix_snap
{
	unsigned long inode;
	unsigned refcnt;
	uint8_t type; /* SOCK_STREAM = 1 */
	uint8_t st;   /* 01 connected, 0A listen, 07 unbound-ish */
	char path[108];
	size_t path_len;
	int is_abstract;
};

int sock_stream_inet_walk(int (*cb)(const struct sock_stream_inet_snap *s,
				    void *ctx),
			  void *ctx);
int sock_stream_unix_walk(int (*cb)(const struct sock_stream_unix_snap *s,
				    void *ctx),
			  void *ctx);
