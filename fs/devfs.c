// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Virtual Device Filesystem (/dev)
 * Copyright (C) 2025 Iván Rodriguez
 *
 * Implements Unix "everything is a file" philosophy
 */

#include "devfs.h"
#include <ir0/memory/allocator.h>
#include <ir0/memory/kmem.h>
#include <ir0/vga.h>
#include <drivers/video/typewriter.h>
#include <drivers/audio/sound_blaster.h>
#include <drivers/IO/ps2_mouse.h>
#include <net/rtl8139.h>
#include <drivers/storage/ata.h>
#include <string.h>
#include <ir0/logging.h>

// Device registry
#define MAX_DEV_NODES 64
static devfs_node_t *dev_nodes[MAX_DEV_NODES];
static int num_dev_nodes = 0;

int64_t dev_null_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)buf; (void)count; (void)offset;
    return 0;  // Always returns EOF
}

int64_t dev_null_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)buf; (void)offset;
    return count;  // Accepts all data, discards it
}

// ============ ZERO DEVICE ============
int64_t dev_zero_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    memset(buf, 0, count);
    return count;
}

int64_t dev_zero_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)buf; (void)offset;
    return count;  // Discard data like /dev/null
}

int64_t dev_console_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    // For now, no console input implementation
    return 0;
}

int64_t dev_console_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    const char *str = (const char *)buf;
    for (size_t i = 0; i < count && i < 1024; i++) {
        if (str[i] == '\n')
            typewriter_vga_print("\n", 0x0F);
        else
            typewriter_vga_print_char(str[i], 0x0F);
    }
    return count;
}

// ============ KERNEL MESSAGE DEVICE ============
static char kmsg_buffer[4096];
static size_t kmsg_head = 0;
static size_t kmsg_tail = 0;

int64_t dev_kmsg_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    const char *msg = (const char *)buf;
    
    // Add to circular buffer
    for (size_t i = 0; i < count && i < 256; i++) {
        kmsg_buffer[kmsg_head] = msg[i];
        kmsg_head = (kmsg_head + 1) % sizeof(kmsg_buffer);
        if (kmsg_head == kmsg_tail) {
            kmsg_tail = (kmsg_tail + 1) % sizeof(kmsg_buffer);  // Overwrite old
        }
    }
    return count;
}

int64_t dev_kmsg_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    char *out_buf = (char *)buf;
    size_t read_count = 0;
    
    while (read_count < count && kmsg_tail != kmsg_head) {
        out_buf[read_count] = kmsg_buffer[kmsg_tail];
        kmsg_tail = (kmsg_tail + 1) % sizeof(kmsg_buffer);
        read_count++;
    }
    
    return read_count;
}

// ============ AUDIO DEVICE ============
int64_t dev_audio_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    // Send audio data to Sound Blaster
    // For now, just accept the data
    return count;
}

int64_t dev_audio_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
    (void)entry;
    switch (request) {
        case AUDIO_SET_VOLUME:
            // Set volume using arg
            break;
        case AUDIO_GET_VOLUME:
            // Return current volume
            break;
        case AUDIO_PLAY:
            // Start playback
            break;
        case AUDIO_STOP:
            // Stop playback
            break;
    }
    return 0;
}

int64_t dev_audio_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)buf; (void)count; (void)offset;
    return 0;  // Audio input not implemented yet
}

// ============ MOUSE DEVICE ============
int64_t dev_mouse_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    if (count < sizeof(int) * 3) return 0;
    
    // Return mouse state: x, y, buttons
    int *mouse_data = (int *)buf;
    mouse_data[0] = 0;  // x
    mouse_data[1] = 0;  // y  
    mouse_data[2] = 0;  // buttons
    
    return sizeof(int) * 3;
}

int64_t dev_mouse_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
    (void)entry; (void)arg;
    switch (request) {
        case MOUSE_GET_STATE:
            // Return current mouse state
            break;
        case MOUSE_SET_SENSITIVITY:
            // Set mouse sensitivity
            break;
    }
    return 0;
}

// ============ NETWORK DEVICE ============
int64_t dev_net_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    const char *cmd = (const char *)buf;
    
    // Parse network commands (ping, ifconfig, etc.)
    if (strncmp(cmd, "ping ", 5) == 0) {
        // Send ping
    } else if (strncmp(cmd, "ifconfig", 8) == 0) {
        // Configure interface
    }
    
    return count;
}

int64_t dev_net_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)buf; (void)count; (void)offset;
    return 0;  // Network status reading not implemented yet
}

int64_t dev_net_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
    (void)entry; (void)arg;
    switch (request) {
        case NET_SEND_PING:
            break;
        case NET_GET_CONFIG:
            break;
        case NET_SET_CONFIG:
            break;
    }
    return 0;
}

// ============ DISK DEVICE ============
int64_t dev_disk_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry;
    // Read from disk at offset
    // For now, just zero the buffer
    memset(buf, 0, count);
    return count;
}

int64_t dev_disk_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)buf; (void)offset;
    // Write to disk at offset
    return count;
}

int64_t dev_disk_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
    (void)entry; (void)arg;
    switch (request) {
        case DISK_READ_SECTOR:
            break;
        case DISK_WRITE_SECTOR:
            break;
        case DISK_GET_GEOMETRY:
            break;
    }
    return 0;
}

// ============ DEVICE OPERATIONS TABLES ============
static const devfs_ops_t null_ops = {
    .read = dev_null_read,
    .write = dev_null_write,
};

static const devfs_ops_t zero_ops = {
    .read = dev_zero_read,
    .write = dev_zero_write,
};

static const devfs_ops_t console_ops = {
    .read = dev_console_read,
    .write = dev_console_write,
};

static const devfs_ops_t kmsg_ops = {
    .read = dev_kmsg_read,
    .write = dev_kmsg_write,
};

static const devfs_ops_t audio_ops = {
    .read = dev_audio_read,
    .write = dev_audio_write,
    .ioctl = dev_audio_ioctl,
};

static const devfs_ops_t mouse_ops = {
    .read = dev_mouse_read,
    .ioctl = dev_mouse_ioctl,
};

static const devfs_ops_t net_ops = {
    .read = dev_net_read,
    .write = dev_net_write,
    .ioctl = dev_net_ioctl,
};

static const devfs_ops_t disk_ops = {
    .read = dev_disk_read,
    .write = dev_disk_write,
    .ioctl = dev_disk_ioctl,
};

// ============ DEVICE NODE DEFINITIONS ============
devfs_node_t dev_null = {
    .entry = { .name = "null", .mode = 0666, .device_id = 1 },
    .ops = &null_ops,
    .ref_count = 0
};

devfs_node_t dev_zero = {
    .entry = { .name = "zero", .mode = 0666, .device_id = 2 },
    .ops = &zero_ops,
    .ref_count = 0
};

devfs_node_t dev_console = {
    .entry = { .name = "console", .mode = 0620, .device_id = 3 },
    .ops = &console_ops,
    .ref_count = 0
};

devfs_node_t dev_tty = {
    .entry = { .name = "tty", .mode = 0620, .device_id = 4 },
    .ops = &console_ops,
    .ref_count = 0
};

devfs_node_t dev_kmsg = {
    .entry = { .name = "kmsg", .mode = 0600, .device_id = 5 },
    .ops = &kmsg_ops,
    .ref_count = 0
};

devfs_node_t dev_audio = {
    .entry = { .name = "audio", .mode = 0660, .device_id = 6 },
    .ops = &audio_ops,
    .ref_count = 0
};

devfs_node_t dev_mouse = {
    .entry = { .name = "mouse", .mode = 0660, .device_id = 7 },
    .ops = &mouse_ops,
    .ref_count = 0
};

devfs_node_t dev_net = {
    .entry = { .name = "net", .mode = 0660, .device_id = 8 },
    .ops = &net_ops,
    .ref_count = 0
};

devfs_node_t dev_disk = {
    .entry = { .name = "disk", .mode = 0660, .device_id = 9 },
    .ops = &disk_ops,
    .ref_count = 0
};

// ============ DEVFS MANAGEMENT ============
int devfs_init(void)
{
    // Register standard devices
    dev_nodes[num_dev_nodes++] = &dev_null;
    dev_nodes[num_dev_nodes++] = &dev_zero;
    dev_nodes[num_dev_nodes++] = &dev_console;
    dev_nodes[num_dev_nodes++] = &dev_tty;
    dev_nodes[num_dev_nodes++] = &dev_kmsg;
    dev_nodes[num_dev_nodes++] = &dev_audio;
    dev_nodes[num_dev_nodes++] = &dev_mouse;
    dev_nodes[num_dev_nodes++] = &dev_net;
    dev_nodes[num_dev_nodes++] = &dev_disk;
    
    return 0;
}

devfs_node_t *devfs_find_node(const char *path)
{
    if (!path || strncmp(path, "/dev/", 5) != 0)
        return NULL;
    
    const char *name = path + 5;  // Skip "/dev/"
    
    for (int i = 0; i < num_dev_nodes; i++) {
        if (strcmp(dev_nodes[i]->entry.name, name) == 0) {
            return dev_nodes[i];
        }
    }
    
    return NULL;
}

devfs_node_t *devfs_find_node_by_id(uint32_t device_id)
{
    for (int i = 0; i < num_dev_nodes; i++) {
        if (dev_nodes[i] && dev_nodes[i]->entry.device_id == device_id) {
            return dev_nodes[i];
        }
    }
    return NULL;
}

int devfs_register_device(const char *name, const devfs_ops_t *ops, uint32_t mode)
{
    if (num_dev_nodes >= MAX_DEV_NODES)
        return -1;
    
    devfs_node_t *node = kmalloc(sizeof(devfs_node_t));
    if (!node)
        return -1;
    
    node->entry.name = name;
    node->entry.mode = mode;
    node->entry.device_id = num_dev_nodes + 100;  // Dynamic IDs
    node->ops = ops;
    node->ref_count = 0;
    
    dev_nodes[num_dev_nodes++] = node;
    return 0;
}
