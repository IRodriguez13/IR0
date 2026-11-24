/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: devfs_input.c
 * Description: mouse and evdev input device nodes
 */

#include "devfs_internal.h"

static int devfs_events0_can_write(devfs_entry_t *entry, pid_t pid)
{
    (void)entry;
    (void)pid;
    return 0;
}

static int devfs_events0_can_read(devfs_entry_t *entry, pid_t pid)
{
    (void)entry;
    (void)pid;
    return ir0_input_poll() ? 1 : 0;
}
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
    (void)input_register_device(input_i8042_dev());
}

int input_is_event_device(uint32_t device_id)
{
    return device_id == dev_events0.entry.device_id ||
           device_id == dev_input_event0.entry.device_id;
}
/*
 * /dev/events0 - Linux evdev input events (keyboard, input(4))
 * read: struct input_event (type, code, value)
 */
static int64_t dev_events0_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    struct input_event ev_buf[16];
    size_t max_ev;
    size_t n;
    size_t i;

    (void)entry;
    (void)offset;
    if (count < sizeof(struct input_event))
        return 0;

    max_ev = count / sizeof(struct input_event);
    if (max_ev > 16)
        max_ev = 16;

    n = 0;
    for (i = 0; i < max_ev; i++)
    {
        struct ir0_input_event ir0_ev;
        int rc = ir0_input_read_event(&ir0_ev);

        if (rc <= 0)
            break;
        ev_buf[i].time.tv_sec = (time_t)(ir0_ev.timestamp_ms / 1000);
        ev_buf[i].time.tv_usec = (suseconds_t)((ir0_ev.timestamp_ms % 1000) * 1000);
        ev_buf[i].type = ir0_ev.type;
        ev_buf[i].code = ir0_ev.code;
        ev_buf[i].value = ir0_ev.value;
        n++;
    }

    if (n == 0)
    {
        if (input_read_is_nonblock())
            return -EAGAIN;
        return 0;
    }

    {
        size_t bytes = n * sizeof(struct input_event);
        memcpy(buf, ev_buf, bytes);
        return (int64_t)bytes;
    }
}

static void evdev_set_bit(unsigned long *bits, size_t nlongs, unsigned int bit)
{
    unsigned int idx = bit / (8u * sizeof(unsigned long));
    unsigned int off = bit % (8u * sizeof(unsigned long));

    if (idx >= nlongs)
        return;
    bits[idx] |= (1UL << off);
}

static int64_t dev_events0_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
    struct ir0_input_caps caps;
    unsigned dir;
    unsigned type;
    unsigned nr;
    unsigned size;

    (void)entry;

    if (request == IR0_INPUT_IOCTL_GET_CAPS)
    {
        if (!arg)
            return -EFAULT;
        ir0_input_get_caps(&caps);
        if (copy_to_user(arg, &caps, sizeof(caps)) != 0)
            return -EFAULT;
        return 0;
    }

    if (request == IR0_INPUT_IOCTL_INJECT)
    {
#if CONFIG_TEST_INPUT_INJECT
        struct ir0_input_event ev;

        if (!arg)
            return -EFAULT;
        if (copy_from_user(&ev, arg, sizeof(ev)) != 0)
            return -EFAULT;
        {
            int rc = ir0_input_inject_event(ev.type, ev.code, ev.value);

            if (rc < 0)
                return rc;
            /* Linux clients expect a SYN_REPORT marker after injected edges. */
            if (ev.type != EV_SYN)
                (void)ir0_input_inject_event(EV_SYN, SYN_REPORT, 0);
            return rc;
        }
#else
        (void)arg;
        return -ENOTTY;
#endif
    }

    dir = (unsigned)_IOC_DIR(request);
    type = (unsigned)_IOC_TYPE(request);
    nr = (unsigned)_IOC_NR(request);
    size = (unsigned)_IOC_SIZE(request);

    if (type == (unsigned)'E' && dir == _IOC_READ && nr == _IOC_NR(EVIOCGVERSION))
    {
        int version = EV_VERSION;

        if (!arg || size < sizeof(int))
            return -EINVAL;
        if (copy_to_user(arg, &version, sizeof(version)) != 0)
            return -EFAULT;
        return 0;
    }

    if (type == (unsigned)'E' && dir == _IOC_READ && nr >= _IOC_NR(EVIOCGBIT(0, 0)) &&
        nr < _IOC_NR(EVIOCGBIT(0, 0)) + EV_CNT)
    {
        unsigned long bits[8];
        unsigned ev = nr - _IOC_NR(EVIOCGBIT(0, 0));
        size_t nlongs;
        size_t copy_n;

        if (!arg || size == 0)
            return -EINVAL;
        memset(bits, 0, sizeof(bits));
        nlongs = sizeof(bits) / sizeof(bits[0]);

        if (ev == 0)
        {
            /* Bitmap of supported event types. */
            evdev_set_bit(bits, nlongs, EV_SYN);
            evdev_set_bit(bits, nlongs, EV_KEY);
            evdev_set_bit(bits, nlongs, EV_REL);
        }
        else if (ev == EV_KEY)
        {
            unsigned k;

            for (k = 1; k < (unsigned)KEY_MAX && k < 256u; k++)
                evdev_set_bit(bits, nlongs, k);
            evdev_set_bit(bits, nlongs, BTN_LEFT);
            evdev_set_bit(bits, nlongs, BTN_RIGHT);
            evdev_set_bit(bits, nlongs, BTN_MIDDLE);
            evdev_set_bit(bits, nlongs, BTN_SIDE);
            evdev_set_bit(bits, nlongs, BTN_EXTRA);
        }
        else if (ev == EV_REL)
        {
            evdev_set_bit(bits, nlongs, REL_X);
            evdev_set_bit(bits, nlongs, REL_Y);
            evdev_set_bit(bits, nlongs, REL_WHEEL);
        }
        else if (ev == EV_SYN)
        {
            evdev_set_bit(bits, nlongs, SYN_REPORT);
        }
        /* other EV_* → empty bitmap (supported) */

        copy_n = size;
        if (copy_n > sizeof(bits))
            copy_n = sizeof(bits);
        if (copy_to_user(arg, bits, copy_n) != 0)
            return -EFAULT;
        return 0;
    }

    if (type == (unsigned)'E' && dir == _IOC_READ && nr == _IOC_NR(EVIOCGID))
    {
        struct input_id id;

        if (!arg || size < sizeof(id))
            return -EINVAL;
        if (input_device_id(&id) != 0)
            return -ENODEV;
        if (copy_to_user(arg, &id, sizeof(id)) != 0)
            return -EFAULT;
        return 0;
    }

    if (type == (unsigned)'E' && dir == _IOC_READ && nr == _IOC_NR(EVIOCGNAME(0)))
    {
        const char *name;
        char tmp[64];
        size_t full;
        size_t ncopy;

        if (!arg || size == 0)
            return -EINVAL;
        name = input_device_name();
        if (!name)
            return -ENODEV;
        full = strlen(name) + 1u;
        ncopy = full < size ? full : size;
        if (ncopy > sizeof(tmp))
            ncopy = sizeof(tmp);
        memcpy(tmp, name, ncopy);
        if (copy_to_user(arg, tmp, ncopy) != 0)
            return -EFAULT;
        return (int64_t)ncopy;
    }

    if (type == (unsigned)'E' && dir == _IOC_READ && nr == _IOC_NR(EVIOCGPROP(0)))
    {
        unsigned long bits;
        size_t ncopy;

        /* INPUT_PROP_POINTER: this node is the pointer device. */
        bits = 1ul;
        if (!arg || size == 0)
            return -EINVAL;
        ncopy = size < sizeof(bits) ? size : sizeof(bits);
        if (copy_to_user(arg, &bits, ncopy) != 0)
            return -EFAULT;
        return 0;
    }

    /* EVIOCGKEY: _IOC(READ, 'E', 0x18, len) — current key bitmap. */
    if (type == (unsigned)'E' && dir == _IOC_READ && nr == _IOC_NR(EVIOCGKEY(0)))
    {
        unsigned char bitmap[(KEY_CNT + 7u) / 8u];
        size_t stored;
        size_t ncopy;

        if (!arg || size == 0)
        {
            return -EINVAL;
        }
        stored = input_key_state(input_i8042_dev(), bitmap, sizeof(bitmap));
        ncopy = (size_t)size < stored ? (size_t)size : stored;
        if (copy_to_user(arg, bitmap, ncopy) != 0)
        {
            return -EFAULT;
        }
        return 0;
    }

    /*
     * EVIOCGABS(0) .. EVIOCGABS(ABS_MAX) are nr 0x40 .. 0x7f.
     * This node is a relative pointer and has no absolute axes.
     */
    if (type == (unsigned)'E' && dir == _IOC_READ &&
        nr >= 0x40u && nr <= 0x7fu)
    {
        return -EINVAL;
    }

    return -ENOTTY;
}

static int64_t dev_events0_open(devfs_entry_t *entry, int flags)
{
	(void)entry;
	(void)flags;
	input_events_reader_open();
	return 0;
}

static int64_t dev_events0_close(devfs_entry_t *entry)
{
	(void)entry;
	input_events_reader_close();
	return 0;
}

static const devfs_ops_t events0_ops = {
    .read = dev_events0_read,
    .write = NULL,
    .ioctl = dev_events0_ioctl,
    .open = dev_events0_open,
    .close = dev_events0_close,
    .can_read = devfs_events0_can_read,
    .can_write = devfs_events0_can_write,
};

devfs_node_t dev_events0 = {
    .entry = { .name = "events0", .mode = 0660, .device_id = 16 },
    .ops = &events0_ops,
    .ref_count = 0,
};

/* Linux evdev node (/dev/input/event0, input(4)). */
devfs_node_t dev_input_event0 = {
    .entry = { .name = "input/event0", .mode = 0660, .device_id = 45 },
    .ops = &events0_ops,
    .ref_count = 0,
};
devfs_node_t dev_mouse = {
    .entry = { .name = "mouse", .mode = 0660, .device_id = 7 },
    .ops = &mouse_ops,
    .ref_count = 0
};
