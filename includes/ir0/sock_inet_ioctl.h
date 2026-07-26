/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: sock_inet_ioctl.h
 * Description: Minimal Linux SIOCGIF* ioctls for BusyBox ifconfig (AF_INET sockets).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#include <ir0/types.h>

/*
 * Linux x86-64 sockios.h request numbers (read-only subset for ifconfig -a).
 * Source: include/uapi/linux/sockios.h
 */
#define IR0_SIOCGIFCONF   0x8912
#define IR0_SIOCGIFFLAGS  0x8913
#define IR0_SIOCGIFADDR   0x8915
#define IR0_SIOCGIFBRDADDR 0x8919
#define IR0_SIOCGIFNETMASK 0x891b
#define IR0_SIOCGIFMTU    0x8921
#define IR0_SIOCGIFHWADDR 0x8927
#define IR0_SIOCGIFINDEX  0x8933

int64_t sock_inet_ioctl(uint64_t request, void *arg);
