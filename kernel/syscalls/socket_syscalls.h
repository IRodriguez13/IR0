/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: socket_syscalls.h
 * Description: socket syscalls (split from syscalls.c)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/types.h>
#include <stddef.h>
#include <ir0/socket.h>

typedef uint32_t socklen_t;

int64_t sys_socket(int domain, int type, int protocol);
int64_t sys_bind(int fd, const struct sockaddr *addr, socklen_t addrlen);
ssize_t sys_sendto(int fd, const void *buf, size_t len, int flags,
		   const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t sys_recvfrom(int fd, void *buf, size_t len, int flags,
		     struct sockaddr *src_addr, socklen_t *addrlen);
int64_t sys_connect(int fd, const struct sockaddr *addr, socklen_t addrlen);
int64_t sys_listen(int fd, int backlog);
int64_t sys_accept(int fd, struct sockaddr *addr, socklen_t *addrlen);
int64_t sys_socketpair(int domain, int type, int protocol, int *sv);
