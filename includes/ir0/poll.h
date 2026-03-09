/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - poll(2) ABI
 * Event flags y estructura para la syscall poll.
 */

#ifndef _IR0_POLL_H
#define _IR0_POLL_H

#include <stddef.h>

#define POLLIN   0x0001
#define POLLOUT  0x0004
#define POLLERR  0x0008
#define POLLHUP  0x0010

struct pollfd {
	int fd;
	short events;
	short revents;
};

#endif
