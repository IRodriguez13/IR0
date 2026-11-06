/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: devfs_internal.h
 * Description: Private helpers shared by the split /dev backend.
 */

#pragma once

#include "devfs.h"
#include <ir0/kmem.h>
#include <ir0/logging.h>
#include <ir0/errno.h>
#include <config.h>
#include <ir0/console_backend.h>
#include <ir0/console.h>
#if CONFIG_ENABLE_SOUND
#include <ir0/audio_backend.h>
#endif
#include <ir0/input_backend.h>
#if CONFIG_ENABLE_NETWORKING
#include <ir0/net.h>
#endif
#include <ir0/blockdev.h>
#include <ir0/partition.h>
#include <string.h>
#include <ir0/clock.h>
#include <ir0/process.h>
#include <ir0/signals.h>
#include <ir0/arch_cpu.h>
#include <ir0/klog.h>
#include <ir0/serial_io.h>
#include <ir0/ipc.h>
#if CONFIG_ENABLE_BLUETOOTH
#include <ir0/bluetooth.h>
#endif
#include <ir0/video_backend.h>
#include <ir0/copy_user.h>
#include <ir0/input.h>
#include <ir0/credentials.h>
#include <ir0/fb.h>
#include <ir0/ktm/userdev.h>
#include <ir0/io_async.h>
#include <linux/kd.h>
#include <linux/keyboard.h>
#include <linux/vt.h>
#include "vfs.h"

int dev_net_pid_has_ready_ping(pid_t pid);
void devfs_register_disk_topology(void);

#define DEVFS_DISK_AGGREGATE_ID  9
#define DEVFS_DISK_BASE_ID       20

/* Per-node context for whole-disk and partition /dev/hd* handlers. */
typedef struct devfs_disk_ctx {
    uint8_t disk_id;
    uint8_t is_whole;
    uint8_t partition_number;
} devfs_disk_ctx_t;
