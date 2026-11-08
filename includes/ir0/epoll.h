/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: epoll.h
 * Description: epoll instance lifetime hooks for the fd resource layer.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

/* Reference an epoll instance held by an fd (kernel/syscalls/epoll_syscalls.c). */
void epoll_acquire(void *epoll_state);
void epoll_release(void *epoll_state);
void epoll_release_fd(void *epoll_state);
