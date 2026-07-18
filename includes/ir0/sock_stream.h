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

struct sock_stream;

struct sock_stream *sock_stream_create(int family);
void sock_stream_release(struct sock_stream *s);
int sock_stream_bind_unix(struct sock_stream *s, const char *path);
int sock_stream_listen(struct sock_stream *s, int backlog);
int sock_stream_connect_unix(struct sock_stream *s, const char *path);
struct sock_stream *sock_stream_accept(struct sock_stream *s);
int sock_stream_bind_inet(struct sock_stream *s, uint16_t port);
int sock_stream_connect_inet(struct sock_stream *s, uint32_t addr, uint16_t port);
ssize_t sock_stream_send(struct sock_stream *s, const void *buf, size_t len);
ssize_t sock_stream_recv(struct sock_stream *s, void *buf, size_t len);
int sock_stream_is(const void *ptr);
/* Connected AF_UNIX stream pair without pathname (socketpair). */
int sock_stream_socketpair(struct sock_stream **a_out, struct sock_stream **b_out);
