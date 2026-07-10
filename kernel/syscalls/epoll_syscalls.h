/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: epoll_syscalls.h
 * Description: Minimal epoll + pselect6 declarations.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/types.h>
#include <ir0/select.h>
#include <ir0/time.h>
#include <stdint.h>

struct epoll_event;

int64_t sys_epoll_create1(int flags);
int64_t sys_epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
int64_t sys_epoll_wait(int epfd, struct epoll_event *events, int maxevents,
		       int timeout);
int64_t sys_epoll_pwait(int epfd, struct epoll_event *events, int maxevents,
			int timeout, const void *sigmask, size_t sigsetsize);
int64_t sys_pselect6(int nfds, fd_set *readfds, fd_set *writefds,
		     fd_set *exceptfds, const struct timespec *timeout,
		     const void *sigmask);
void epoll_release_fd(void *epoll_state);
