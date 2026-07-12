/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: virtio_9p.h
 * Description: Facade for QEMU virtio-9p host directory share.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#define HOSTSHARE_MOUNT_TAG "ir0share"
#define HOSTSHARE_MOUNT_PATH "/mnt/host"

int virtio_9p_init(void);
int virtio_9p_ready(void);
int virtio_9p_write_file(const char *relpath, const void *buf, size_t len);
int virtio_9p_read_file(const char *relpath, void *buf, size_t maxlen);
