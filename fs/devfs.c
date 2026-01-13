/* SPDX-License-Identifier: GPL-3.0-only */
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
#include <ir0/keyboard.h>

/* Device registry */
#define MAX_DEV_NODES 64
static devfs_node_t *dev_nodes[MAX_DEV_NODES];
static int num_dev_nodes = 0;

int64_t dev_null_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)buf; (void)count; (void)offset;
    /* Always returns EOF */
    return 0;
}

int64_t dev_null_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)buf; (void)offset;
    /* Accepts all data, discards it */
    return count;
}

int64_t dev_zero_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    memset(buf, 0, count);
    return count;
}

int64_t dev_zero_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)buf; (void)offset;
    return count;
}

int64_t dev_console_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    char *buffer = (char *)buf;
    size_t bytes_read = 0;
    
    /* Read characters from keyboard buffer */
    while (bytes_read < count && keyboard_buffer_has_data())
    {
        char c = keyboard_buffer_get();
        if (c != 0)
        {
            buffer[bytes_read++] = c;
        }
        else
        {
            /* No more data available */
            break;
        }
    }
    
    return (int64_t)bytes_read;
}

int64_t dev_console_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    const char *str = (const char *)buf;
    for (size_t i = 0; i < count && i < 1024; i++)
    {
        if (str[i] == '\n')
            typewriter_vga_print("\n", 0x0F);
        else
            typewriter_vga_print_char(str[i], 0x0F);
    }
    return count;
}

/* Circular buffer for kernel messages */
#define KMSG_BUFFER_SIZE 4096
static char kmsg_buffer[KMSG_BUFFER_SIZE];
static size_t kmsg_head = 0;
static size_t kmsg_tail = 0;
static size_t kmsg_count = 0;  /* Number of bytes in buffer */

int64_t dev_kmsg_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    const char *msg = (const char *)buf;
    size_t written = 0;
    
    /* Add to circular buffer */
    for (size_t i = 0; i < count && i < 256; i++)
    {
        kmsg_buffer[kmsg_head] = msg[i];
        kmsg_head = (kmsg_head + 1) % KMSG_BUFFER_SIZE;
        
        if (kmsg_count < KMSG_BUFFER_SIZE)
        {
            kmsg_count++;
        }
        else
        {
            /* Buffer is full, overwrite oldest data */
            kmsg_tail = (kmsg_tail + 1) % KMSG_BUFFER_SIZE;
        }
        written++;
    }
    
    return (int64_t)written;
}

int64_t dev_kmsg_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    char *out_buf = (char *)buf;
    size_t read_count = 0;
    
    /* Read from circular buffer */
    while (read_count < count && kmsg_count > 0)
    {
        out_buf[read_count] = kmsg_buffer[kmsg_tail];
        kmsg_tail = (kmsg_tail + 1) % KMSG_BUFFER_SIZE;
        kmsg_count--;
        read_count++;
    }
    
    return (int64_t)read_count;
}

int64_t dev_audio_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    
    if (!sb16_is_available())
    {
        /* Sound Blaster not available, accept data but don't process */
        return (int64_t)count;
    }
    
    /* Ensure speaker is on for audio output */
    sb16_speaker_on();
    
    /* For now, accept the data */
    /* TODO: Implement actual audio playback using sb16_play_sample() 
     * when sample format is determined */
    return (int64_t)count;
}

int64_t dev_audio_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
    (void)entry;
    switch (request)
    {
        case AUDIO_SET_VOLUME:
            /* Set volume using arg */
            break;
        case AUDIO_GET_VOLUME:
            /* Return current volume */
            break;
        case AUDIO_PLAY:
            /* Start playback */
            break;
        case AUDIO_STOP:
            /* Stop playback */
            break;
    }
    return 0;
}

int64_t dev_audio_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)buf; (void)count; (void)offset;
    /* Audio input not implemented yet */
    return 0;
}

int64_t dev_mouse_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    if (count < sizeof(int) * 3)
        return 0;
    
    /* Return mouse state: x, y, buttons */
    int *mouse_data = (int *)buf;
    
    if (ps2_mouse_is_available())
    {
        ps2_mouse_state_t *st = ps2_mouse_get_state();
        mouse_data[0] = (int)st->x;
        mouse_data[1] = (int)st->y;
        /* Encode button state: L=bit0, R=bit1, M=bit2 */
        mouse_data[2] = (st->left_button ? 1 : 0) |
                       (st->right_button ? 2 : 0) |
                       (st->middle_button ? 4 : 0);
    }
    else
    {
        mouse_data[0] = 0;
        mouse_data[1] = 0;
        mouse_data[2] = 0;
    }
    
    return sizeof(int) * 3;
}

int64_t dev_mouse_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
    (void)entry; (void)arg;
    switch (request)
    {
        case MOUSE_GET_STATE:
            /* Return current mouse state */
            break;
        case MOUSE_SET_SENSITIVITY:
            /* Set mouse sensitivity */
            break;
    }
    return 0;
}

int64_t dev_net_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    const char *cmd = (const char *)buf;
    
    /* Parse network commands (ping, ifconfig, etc.) */
    if (strncmp(cmd, "ping ", 5) == 0)
    {
        /* Send ping */
    }
    else if (strncmp(cmd, "ifconfig", 8) == 0)
    {
        /* Configure interface */
    }
    
    return count;
}

int64_t dev_net_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)buf; (void)count; (void)offset;
    /* Network status reading not implemented yet */
    return 0;
}

int64_t dev_net_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
    (void)entry; (void)arg;
    switch (request)
    {
        case NET_SEND_PING:
            break;
        case NET_GET_CONFIG:
            break;
        case NET_SET_CONFIG:
            break;
    }
    return 0;
}

int64_t dev_disk_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry;
    (void)offset;  /* For now, always return from start */
    
    /* Generate df-like output: Filesystem          Size */
    char output[1024];
    size_t off = 0;
    
    int n = snprintf(output + off, sizeof(output) - off,
                     "Filesystem          Size\n");
    if (n > 0 && (size_t)n < sizeof(output) - off)
        off += (size_t)n;
    
    n = snprintf(output + off, sizeof(output) - off,
                 "----------------------------------\n");
    if (n > 0 && (size_t)n < sizeof(output) - off)
        off += (size_t)n;
    
    int found_drives = 0;
    for (uint8_t i = 0; i < 4; i++)
    {
        if (!ata_drive_present(i))
            continue;
        
        found_drives++;
        char devname[16];
        int len = snprintf(devname, sizeof(devname), "/dev/hd%c", 'a' + i);
        if (len < 0 || len >= (int)sizeof(devname))
            continue;
        
        /* ata_get_size() returns size in 512-byte sectors */
        uint64_t size = ata_get_size(i);
        if (size == 0)
        {
            n = snprintf(output + off, sizeof(output) - off,
                        "%-20s (empty)\n", devname);
        }
        else
        {
            char size_str[32];
            /* Same calculation as sys_df: sectors / (2 * 1024 * 1024) = GB */
            uint64_t size_gb = size / (2 * 1024 * 1024);
            if (size_gb > 0)
            {
                /* Convert uint64_t to string manually since snprintf doesn't support %llu */
                char num_str[32];
                char *p = num_str;
                uint64_t tmp = size_gb;
                if (tmp == 0)
                {
                    *p++ = '0';
                }
                else
                {
                    char rev[32];
                    int idx = 0;
                    while (tmp > 0)
                    {
                        rev[idx++] = '0' + (tmp % 10);
                        tmp /= 10;
                    }
                    while (idx > 0)
                        *p++ = rev[--idx];
                }
                *p = '\0';
                len = snprintf(size_str, sizeof(size_str), "%sG", num_str);
            }
            else
            {
                /* sectors / (2 * 1024) = MB */
                uint64_t size_mb = size / (2 * 1024);
                char num_str[32];
                char *p = num_str;
                uint64_t tmp = size_mb;
                if (tmp == 0)
                {
                    *p++ = '0';
                }
                else
                {
                    char rev[32];
                    int idx = 0;
                    while (tmp > 0)
                    {
                        rev[idx++] = '0' + (tmp % 10);
                        tmp /= 10;
                    }
                    while (idx > 0)
                        *p++ = rev[--idx];
                }
                *p = '\0';
                len = snprintf(size_str, sizeof(size_str), "%sM", num_str);
            }
            
            if (len > 0 && len < (int)sizeof(size_str))
            {
                n = snprintf(output + off, sizeof(output) - off,
                            "%-20s %s\n", devname, size_str);
            }
            else
            {
                n = 0;
            }
        }
        
        if (n > 0 && (size_t)n < sizeof(output) - off)
            off += (size_t)n;
    }
    
    if (found_drives == 0)
    {
        n = snprintf(output + off, sizeof(output) - off,
                    "No drives detected\n");
        if (n > 0 && (size_t)n < sizeof(output) - off)
            off += (size_t)n;
    }
    
    /* Copy to user buffer (respecting offset and count) */
    size_t copy_size = (off < count) ? off : count;
    if (copy_size > 0)
    {
        memcpy(buf, output, copy_size);
    }
    
    return (int64_t)copy_size;
}

int64_t dev_disk_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)buf; (void)offset;
    /* Write to disk at offset */
    return count;
}

int64_t dev_disk_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
    (void)entry; (void)arg;
    switch (request)
    {
        case DISK_READ_SECTOR:
            break;
        case DISK_WRITE_SECTOR:
            break;
        case DISK_GET_GEOMETRY:
            break;
    }
    return 0;
}

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

int devfs_init(void)
{
    /* Register standard devices */
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
    
    const char *name = path + 5;  /* Skip "/dev/" */
    
    for (int i = 0; i < num_dev_nodes; i++)
    {
        if (strcmp(dev_nodes[i]->entry.name, name) == 0)
        {
            return dev_nodes[i];
        }
    }
    
    return NULL;
}

devfs_node_t *devfs_find_node_by_id(uint32_t device_id)
{
    for (int i = 0; i < num_dev_nodes; i++)
    {
        if (dev_nodes[i] && dev_nodes[i]->entry.device_id == device_id)
        {
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
    node->entry.device_id = num_dev_nodes + 100;  /* Dynamic IDs */
    node->ops = ops;
    node->ref_count = 0;
    
    dev_nodes[num_dev_nodes++] = node;
    return 0;
}
