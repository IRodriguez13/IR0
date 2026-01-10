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

// Device filesystem management
int devfs_init(void);
devfs_node_t *devfs_find_node(const char *path);
devfs_node_t *devfs_find_node_by_id(uint32_t device_id);
int devfs_register_device(const char *name, const devfs_ops_t *ops, uint32_t mode);
int devfs_unregister_device(const char *name);

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

#define MOUSE_GET_STATE     0x2001
#define MOUSE_SET_SENSITIVITY 0x2002

#define NET_SEND_PING       0x3001
#define NET_GET_CONFIG      0x3002
#define NET_SET_CONFIG      0x3003

#define DISK_READ_SECTOR    0x4001
#define DISK_WRITE_SECTOR   0x4002
#define DISK_GET_GEOMETRY   0x4003
