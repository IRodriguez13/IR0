/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: devfs_ipc.c
 * Description: IPC and Bluetooth HCI device nodes
 */

#include "devfs_internal.h"

/* IPC device operations */
int64_t dev_ipc_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    
    /* Extract channel ID from driver_data (set in open/ioctl) */
    ipc_channel_t *channel = (ipc_channel_t *)entry->driver_data;
    if (!channel)
        return -1;
    
    /* Read from IPC channel (blocking) */
    return ipc_channel_read(channel, buf, count);
}

int64_t dev_ipc_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    
    /* Extract channel ID from driver_data (set in open/ioctl) */
    ipc_channel_t *channel = (ipc_channel_t *)entry->driver_data;
    if (!channel)
        return -1;
    
    /* Write to IPC channel (blocking) */
    return ipc_channel_write(channel, buf, count);
}

int64_t dev_ipc_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
    if (!entry)
        return -1;
    
    switch (request)
    {
        case IPC_CREATE_CHANNEL:
            if (arg) {
                /* arg points to uint32_t *channel_id (input/output) */
                uint32_t *channel_id_ptr = (uint32_t *)arg;
                uint32_t channel_id = *channel_id_ptr;
                
                /* If channel_id is 0, allocate a new one */
                if (channel_id == 0) {
                    channel_id = ipc_allocate_channel_id();
                }
                
                /* Get or create channel */
                ipc_channel_t *channel = ipc_channel_get_or_create(channel_id);
                if (!channel)
                    return -1;
                
                /* Store channel pointer in driver_data */
                entry->driver_data = (void *)channel;
                
                /* Return channel ID */
                *channel_id_ptr = channel_id;
                
                /* Increment reference count */
                ipc_channel_ref(channel);
                
                return 0;
            }
            return -1;
            
        case IPC_DESTROY_CHANNEL:
            if (entry->driver_data) {
                ipc_channel_t *channel = (ipc_channel_t *)entry->driver_data;
                ipc_channel_unref(channel);
                entry->driver_data = NULL;
                return 0;
            }
            return -1;
            
        case IPC_GET_CHANNEL_ID:
            if (arg && entry->driver_data) {
                ipc_channel_t *channel = (ipc_channel_t *)entry->driver_data;
                uint32_t *channel_id_ptr = (uint32_t *)arg;
                *channel_id_ptr = channel->id;
                return 0;
            }
            return -1;
            
        default:
            return -1;
    }
}

int64_t dev_ipc_open(devfs_entry_t *entry, int flags)
{
    (void)entry; (void)flags;
    /* Channel will be created/opened via ioctl */
    return 0;
}

int64_t dev_ipc_close(devfs_entry_t *entry)
{
    if (!entry)
        return -1;
    
    /* If channel was opened, release reference */
    if (entry->driver_data) {
        ipc_channel_t *channel = (ipc_channel_t *)entry->driver_data;
        ipc_channel_unref(channel);
        entry->driver_data = NULL;
    }
    
    return 0;
}

static const devfs_ops_t ipc_ops = {
    .read = dev_ipc_read,
    .write = dev_ipc_write,
    .ioctl = dev_ipc_ioctl,
    .open = dev_ipc_open,
    .close = dev_ipc_close,
};
#if CONFIG_ENABLE_BLUETOOTH
/* Bluetooth HCI device operations */
int64_t dev_bluetooth_hci_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    return ir0_bt_hci_read((char *)buf, count);
}

int64_t dev_bluetooth_hci_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    return ir0_bt_hci_write((const char *)buf, count);
}

int64_t dev_bluetooth_hci_open(devfs_entry_t *entry, int flags)
{
    (void)entry; (void)flags;
    return ir0_bt_hci_open();
}

int64_t dev_bluetooth_hci_close(devfs_entry_t *entry)
{
    (void)entry;
    return ir0_bt_hci_close();
}

int64_t dev_bluetooth_hci_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
    (void)entry;
    return ir0_bt_hci_ioctl((unsigned int)request, (unsigned long)arg);
}

static const devfs_ops_t bluetooth_hci_ops = {
    .read = dev_bluetooth_hci_read,
    .write = dev_bluetooth_hci_write,
    .open = dev_bluetooth_hci_open,
    .close = dev_bluetooth_hci_close,
    .ioctl = dev_bluetooth_hci_ioctl
};
#endif
devfs_node_t dev_ipc = {
    .entry = { .name = "ipc", .mode = 0666, .device_id = 13 },
    .ops = &ipc_ops,
    .ref_count = 0
};

#if CONFIG_ENABLE_BLUETOOTH
devfs_node_t dev_bluetooth_hci0 = {
    .entry = { .name = "bluetooth/hci0", .mode = 0660, .device_id = 14 },
    .ops = &bluetooth_hci_ops,
    .ref_count = 0
};
#endif
