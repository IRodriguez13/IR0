/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: socket.h
 * Description: POSIX socket types facade (Linux uapi subset)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

#define AF_UNSPEC   0
#define AF_UNIX     1
#define AF_INET     2

#define SOCK_STREAM 1
#define SOCK_DGRAM  2

#define SOL_SOCKET  1

struct sockaddr
{
	uint16_t sa_family;
	char sa_data[14];
};

struct sockaddr_in
{
	uint16_t sin_family;
	uint16_t sin_port;
	uint32_t sin_addr;
	uint8_t sin_zero[8];
};

struct sockaddr_un
{
	uint16_t sun_family;
	char sun_path[108];
};
