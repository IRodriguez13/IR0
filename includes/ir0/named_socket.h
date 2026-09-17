/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef IR0_NAMED_SOCKET_H
#define IR0_NAMED_SOCKET_H

#include <ir0/stat.h>

int named_socket_create(const char *path, mode_t mode);
int named_socket_stat(const char *path, stat_t *buf);
int named_socket_unlink(const char *path);

#endif
