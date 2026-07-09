/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: select.h
 * Description: IR0 Kernel - select(2) ABI (Linux x86-64 musl layout)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>

#define IR0_FD_SETSIZE 1024

typedef unsigned long ir0_fd_mask;

#define IR0_NFDBITS (8 * (int)sizeof(ir0_fd_mask))

typedef struct
{
	ir0_fd_mask fds_bits[IR0_FD_SETSIZE / IR0_NFDBITS];
} fd_set;

#define IR0_FD_ZERO(set) \
	do { \
		unsigned int __i; \
		for (__i = 0; __i < (IR0_FD_SETSIZE / IR0_NFDBITS); __i++) \
			(set)->fds_bits[__i] = 0; \
	} while (0)

#define IR0_FD_SET(fd, set) \
	do { \
		if ((fd) >= 0 && (fd) < IR0_FD_SETSIZE) \
			(set)->fds_bits[(fd) / IR0_NFDBITS] |= \
				(1UL << ((unsigned)(fd) % IR0_NFDBITS)); \
	} while (0)

#define IR0_FD_CLR(fd, set) \
	do { \
		if ((fd) >= 0 && (fd) < IR0_FD_SETSIZE) \
			(set)->fds_bits[(fd) / IR0_NFDBITS] &= \
				~(1UL << ((unsigned)(fd) % IR0_NFDBITS)); \
	} while (0)

#define IR0_FD_ISSET(fd, set) \
	(((fd) >= 0) && ((fd) < IR0_FD_SETSIZE) && \
	 (((set)->fds_bits[(fd) / IR0_NFDBITS] & \
	   (1UL << ((unsigned)(fd) % IR0_NFDBITS))) != 0))
