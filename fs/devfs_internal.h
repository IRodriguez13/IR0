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
#include <ir0/pty_devfs.h>
#include <string.h>

#define PTY_BUF_SIZE 1024
#define DEVFS_PTY_VFS_TAG 0x50545900u

struct pty_ring
{
	char buf[PTY_BUF_SIZE];
	unsigned int head;
	unsigned int tail;
	unsigned int count;
};

struct pty_pair
{
	struct pty_ring m2s;
	struct pty_ring s2m;
	int master_open;
	int master_refs;
	int slave_open;
	int slave_refs;
	int locked;
	int ctty_set;
	pid_t fg_pgid;
	pid_t session_sid;
	struct ir0_winsize winsz;
	struct ir0_termios termios;
	int termios_ready;
};

extern struct pty_pair g_ptys[DEVFS_PTY_MAX];
extern devfs_node_t dev_pts_nodes[DEVFS_PTY_MAX];

/* Device nodes owned by the split /dev backend files. */
extern devfs_node_t dev_tty0;
extern devfs_node_t dev_tty1;
extern devfs_node_t dev_stdin;
extern devfs_node_t dev_stdout;
extern devfs_node_t dev_stderr;
extern devfs_node_t dev_random;
extern devfs_node_t dev_urandom;
extern devfs_node_t dev_full;
extern devfs_node_t dev_fb0;
extern devfs_node_t dev_events0;
extern devfs_node_t dev_input_event0;
extern devfs_node_t dev_serial;
extern devfs_node_t dev_ipc;
#if CONFIG_ENABLE_BLUETOOTH
extern devfs_node_t dev_bluetooth_hci0;
#endif

/* Cross-file helpers for the split /dev backend. */
int devfs_console_can_read(devfs_entry_t *entry, pid_t pid);
int64_t dev_console_ioctl(devfs_entry_t *entry, uint64_t request, void *arg);
void devfs_input_init(void);

int dev_net_pid_has_ready_ping(pid_t pid);
void devfs_register_disk_topology(void);

pid_t devfs_current_pid(void);
int devfs_register_node(devfs_node_t *node);

int pty_ring_pop(struct pty_ring *r, char *dst, size_t n);
int64_t dev_pts_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset);
int64_t pty_master_ioctl_pair(struct pty_pair *p, int slot, uint64_t request, void *arg);

void devfs_pty_register_nodes(void);

extern devfs_node_t dev_ptmx;

#define DEVFS_DISK_AGGREGATE_ID  9
#define DEVFS_DISK_BASE_ID       20

/* Per-node context for whole-disk and partition /dev/hd* handlers. */
typedef struct devfs_disk_ctx {
    uint8_t disk_id;
    uint8_t is_whole;
    uint8_t partition_number;
} devfs_disk_ctx_t;
