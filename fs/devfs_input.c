/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: devfs_input.c
 * Description: /dev/mouse (PS/2 packets of the mouse driver)
 */

#include "devfs_internal.h"

#if CONFIG_ENABLE_MOUSE
static int dev_mouse_can_read(devfs_entry_t *entry, pid_t pid)
{
    (void)entry;
    (void)pid;
	return input_mouse_packet_available() ? 1 : 0;
}
#endif
static int64_t dev_mouse_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
#if CONFIG_ENABLE_MOUSE
    uint8_t packet[3];
    int n;
    static int read_path_logged;
    static uint8_t last_buttons;

    (void)entry;
    (void)offset;
    /* Linux KDrive consumes standard three-byte packets from /dev/mouse. */
    if (!buf || count < sizeof(packet))
        return 0;
    n = input_mouse_read_ps2(packet);
    if (n <= 0)
        return 0;
    memcpy(buf, packet, sizeof(packet));
    if ((packet[0] & 0x07u) != last_buttons)
    {
        last_buttons = packet[0] & 0x07u;
        klog_debug_fmt("INPUT", "DEV_MOUSE_BUTTONS mask=%u",
                       (unsigned)last_buttons);
    }
    if (!read_path_logged)
    {
        read_path_logged = 1;
        klog_debug_fmt("INPUT", "DEV_MOUSE_READ_PATH_OK");
    }
    return (int64_t)sizeof(packet);
#else
    (void)entry; (void)buf; (void)count; (void)offset;
    return -ENODEV;
#endif
}

static int64_t dev_mouse_write(devfs_entry_t *entry, const void *buf,
                               size_t count, off_t offset)
{
#if CONFIG_ENABLE_MOUSE
    static int first_write_logged;

    (void)entry;
    (void)offset;

    if (!input_mouse_is_available())
        return -ENODEV;
    if (!buf && count != 0)
        return -EFAULT;

    if (!first_write_logged)
    {
        first_write_logged = 1;
        klog_debug_fmt("INPUT", "DEV_MOUSE_WRITE bytes=%u", (unsigned)count);
    }

    /*
     * Linux KDrive probes a legacy psaux-style node by writing PS/2
     * initialization commands before consuming packets. IR0 already owns
     * and configures the physical i8042 device in-kernel; forwarding those
     * bytes would race its IRQ packet parser with ACK/ID replies. Accepting
     * the complete command stream is the compatibility contract here: the
     * requested operational state (enabled stream, negotiated packet size)
     * is already maintained by the driver.
     */
    return (int64_t)count;
#else
    (void)entry;
    (void)buf;
    (void)count;
    (void)offset;
    return -ENODEV;
#endif
}

static int64_t dev_mouse_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
#if CONFIG_ENABLE_MOUSE
    (void)entry;
    
    if (!input_mouse_is_available())
    {
        return -ENOTTY;
    }
    
    switch (request)
    {
        case MOUSE_GET_STATE:
            if (arg)
            {
                ir0_mouse_state_t st;
                if (input_mouse_get_state(&st))
                {
                    if (copy_to_user(arg, &st, sizeof(st)) != 0)
                        return -EFAULT;
                    return 0;
                }
            }
            return -ENOTTY;

        case MOUSE_SET_SENSITIVITY:
            if (arg)
            {
                uint8_t sensitivity;
                if (copy_from_user(&sensitivity, arg,
                                   sizeof(sensitivity)) != 0)
                    return -EFAULT;
                /* Sensitivity maps to sample rate: higher = more sensitive */
                /* Typical range: 10-200 samples/sec, default 100 */
                if (sensitivity < 10)
                    sensitivity = 10;
                if (sensitivity > 200)
                    sensitivity = 200;
                if (input_mouse_set_sensitivity(sensitivity))
                {
                    return 0;
                }
            }
            return -ENOTTY;

        default:
            return -ENOTTY;
    }
#else
    (void)entry; (void)request; (void)arg;
    return -ENODEV;
#endif
}
static const devfs_ops_t mouse_ops = {
    .read = dev_mouse_read,
    .write = dev_mouse_write,
    .ioctl = dev_mouse_ioctl,
#if CONFIG_ENABLE_MOUSE
    .can_read = dev_mouse_can_read,
#endif
};

static void dev_mouse_ready_notify(void)
{
    io_async_notify_device(dev_mouse.entry.device_id);
}

void devfs_input_init(void)
{
    input_mouse_set_ready_notifier(dev_mouse_ready_notify);
}

devfs_node_t dev_mouse = {
    .entry = { .name = "mouse", .mode = 0660, .device_id = 7 },
    .ops = &mouse_ops,
    .ref_count = 0
};
