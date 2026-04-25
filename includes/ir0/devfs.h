/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: devfs.h
 * Description: IR0 kernel source/header file
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#include <ir0/types.h>

// Virtual Device Filesystem - /dev
// Implements Unix "everything is a file" for devices

typedef struct {
    const char *name;
    uint32_t mode;        // File permissions
    uint32_t device_id;   // Device identifier
    void *driver_data;    // Driver-specific data
} devfs_entry_t;

// Device operations - polymorphic interface
typedef struct {
    int64_t (*read)(devfs_entry_t *entry, void *buf, size_t count, off_t offset);
    int64_t (*write)(devfs_entry_t *entry, const void *buf, size_t count, off_t offset);
    int64_t (*ioctl)(devfs_entry_t *entry, uint64_t request, void *arg);
    int64_t (*open)(devfs_entry_t *entry, int flags);
    int64_t (*close)(devfs_entry_t *entry);
} devfs_ops_t;

// Device node structure
typedef struct {
    devfs_entry_t entry;
    const devfs_ops_t *ops;
    uint64_t ref_count;
} devfs_node_t;

// Standard device nodes
extern devfs_node_t dev_null;
extern devfs_node_t dev_zero;
extern devfs_node_t dev_console;
extern devfs_node_t dev_tty;
extern devfs_node_t dev_audio;
extern devfs_node_t dev_mouse;
extern devfs_node_t dev_net;
extern devfs_node_t dev_disk;
extern devfs_node_t dev_kmsg;

/* Device filesystem management */
int devfs_init(void);
devfs_node_t *devfs_find_node(const char *path);
devfs_node_t *devfs_find_node_by_id(uint32_t device_id);
int devfs_register_node(devfs_node_t *node);
int devfs_register_device(const char *name, const devfs_ops_t *ops, uint32_t mode);
int devfs_unregister_device(const char *name);
int devfs_fd_can_read(uint32_t device_id, pid_t pid);

// Standard device implementations
int64_t dev_null_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset);
int64_t dev_null_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset);

int64_t dev_zero_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset);
int64_t dev_zero_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset);

int64_t dev_console_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset);
int64_t dev_console_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset);

int64_t dev_kmsg_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset);
int64_t dev_kmsg_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset);

// Audio device operations
int64_t dev_audio_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset);
int64_t dev_audio_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset);
int64_t dev_audio_ioctl(devfs_entry_t *entry, uint64_t request, void *arg);

// Mouse device operations  
int64_t dev_mouse_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset);
int64_t dev_mouse_ioctl(devfs_entry_t *entry, uint64_t request, void *arg);

// Network device operations
int64_t dev_net_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset);
int64_t dev_net_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset);
int64_t dev_net_ioctl(devfs_entry_t *entry, uint64_t request, void *arg);

// Disk device operations
int64_t dev_disk_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset);
int64_t dev_disk_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset);
int64_t dev_disk_ioctl(devfs_entry_t *entry, uint64_t request, void *arg);

// IOCTL requests for different devices
#define AUDIO_SET_VOLUME    0x1001
#define AUDIO_GET_VOLUME    0x1002
#define AUDIO_PLAY          0x1003
#define AUDIO_STOP          0x1004
#define AUDIO_SET_FORMAT    0x1005
#define AUDIO_GET_FORMAT    0x1006

/* PCM format for Doom compatibility (11025 Hz, 8-bit mono default) */
struct audio_format {
    uint32_t sample_rate;   /* 4000-45454 Hz, Doom uses 11025 */
    uint8_t channels;      /* 1=mono, 2=stereo */
    uint8_t bits_per_sample; /* 8 or 16 */
};

#define MOUSE_GET_STATE     0x2001
#define MOUSE_SET_SENSITIVITY 0x2002

#define NET_SEND_PING       0x3001
#define NET_GET_CONFIG      0x3002
#define NET_SET_CONFIG      0x3003
#define NET_GET_PING_RESULT 0x3004

#define DISK_READ_SECTOR    0x4001
#define DISK_WRITE_SECTOR   0x4002
#define DISK_GET_GEOMETRY   0x4003

#define IPC_CREATE_CHANNEL  0x5001
#define IPC_DESTROY_CHANNEL 0x5002
#define IPC_GET_CHANNEL_ID  0x5003

/* Framebuffer (OSDev /dev/fb0, Linux-compatible ioctl) */
#define FBIOGET_VSCREENINFO 0x4600  /* Linux uapi/fb.h */
#define FBIOGET_FSCREENINFO 0x4602
struct fb_var_screeninfo {
    uint32_t xres;           /* visible resolution */
    uint32_t yres;
    uint32_t xres_virtual;
    uint32_t yres_virtual;
    uint32_t xoffset;
    uint32_t yoffset;
    uint32_t bits_per_pixel;
    uint32_t grayscale;
    uint32_t red;
    uint32_t green;
    uint32_t blue;
    uint32_t transp;
    uint32_t nonstd;
    uint32_t activate;
    uint32_t height;
    uint32_t width;
    uint32_t accel_flags;
    uint32_t pixclock;
    uint32_t left_margin;
    uint32_t right_margin;
    uint32_t upper_margin;
    uint32_t lower_margin;
    uint32_t hsync_len;
    uint32_t vsync_len;
    uint32_t sync;
    uint32_t vmode;
    uint32_t rotate;
    uint32_t colorspace;
};

/* Linux fb_fix_screeninfo (for mmap: line_length, smem_len) */
struct fb_fix_screeninfo {
    char id[16];
    unsigned long smem_start;
    uint32_t smem_len;
    uint32_t type;
    uint32_t type_aux;
    uint32_t visual;
    uint16_t xpanstep;
    uint16_t ypanstep;
    uint16_t ywrapstep;
    uint32_t line_length;
    unsigned long mmio_start;
    uint32_t mmio_len;
    uint32_t accel;
    uint16_t capabilities;
    uint16_t reserved[2];
};

#define FB_TYPE_PACKED_PIXELS 0
#define FB_VISUAL_TRUECOLOR 2
#define FB_ACCEL_NONE 0

/* Minimal fb info for IR0 (subset of above) */
struct fb_info_min {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t pitch;
};
