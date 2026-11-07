/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: devfs_tty.c
 * Description: serial and controlling terminal device nodes
 */

#include "devfs_internal.h"

static int devfs_serial_can_read(devfs_entry_t *entry, pid_t pid)
{
    (void)entry;
    (void)pid;
    return 0;
}

static int devfs_serial_can_write(devfs_entry_t *entry, pid_t pid)
{
    (void)entry;
    (void)pid;
    return 1;
}
/*
 * /dev/serial - Write-only debug output to COM1 (for debug bins via syscalls)
 */
static int64_t dev_serial_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)buf; (void)count; (void)offset;
    return 0;
}

static int64_t dev_serial_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    if (!buf)
        return -EFAULT;
    const char *p = (const char *)buf;
    for (size_t i = 0; i < count; i++)
        serial_putchar(p[i]);
    return (int64_t)count;
}

static const devfs_ops_t serial_ops = {
    .read = dev_serial_read,
    .write = dev_serial_write,
    .can_read = devfs_serial_can_read,
    .can_write = devfs_serial_can_write,
};

devfs_node_t dev_serial = {
    .entry = { .name = "serial", .mode = 0220, .device_id = 42 },
    .ops = &serial_ops,
    .ref_count = 0,
};

/*
 * /dev/tty is the controlling terminal of the calling session, not an alias
 * for /dev/console.  Linux resolves it dynamically (drivers/tty/tty_io.c);
 * do the same so unmodified readpassphrase/sudo/OpenDoas reach their PTY.
 */
static struct pty_pair *dev_tty_current_pty(int *slot_out)
{
	int i;

	if (!current_process)
		return NULL;
	for (i = 0; i < DEVFS_PTY_MAX; i++)
	{
		if (g_ptys[i].ctty_set &&
		    g_ptys[i].session_sid == current_process->sid)
		{
			if (slot_out)
				*slot_out = i;
			return &g_ptys[i];
		}
	}
	return NULL;
}

static int dev_tty_uses_console(void)
{
	return current_process &&
	       ir0_console_has_ctty_for_sid((int32_t)current_process->sid);
}

static int64_t dev_tty_read(devfs_entry_t *entry, void *buf, size_t count,
			    off_t offset)
{
	struct pty_pair *p = dev_tty_current_pty(NULL);

	if (p)
		return pty_ring_pop(&p->m2s, (char *)buf, count);
	if (dev_tty_uses_console())
		return dev_console_read(entry, buf, count, offset);
	return -ENXIO;
}

static int64_t dev_tty_write(devfs_entry_t *entry, const void *buf,
			     size_t count, off_t offset)
{
	int slot = -1;

	if (dev_tty_current_pty(&slot))
		return dev_pts_write(&dev_pts_nodes[slot].entry, buf, count, offset);
	if (dev_tty_uses_console())
		return dev_console_write(entry, buf, count, offset);
	return -ENXIO;
}

static int64_t dev_tty_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
	struct pty_pair *p;
	int slot = -1;

	p = dev_tty_current_pty(&slot);
	if (p)
		return pty_master_ioctl_pair(p, slot, request, arg);
	if (dev_tty_uses_console())
		return dev_console_ioctl(entry, request, arg);
	return -ENXIO;
}

static int64_t dev_tty_open(devfs_entry_t *entry, int flags)
{
	(void)entry;
	(void)flags;
	return dev_tty_current_pty(NULL) || dev_tty_uses_console() ? 0 : -ENXIO;
}

static int dev_tty_can_read(devfs_entry_t *entry, pid_t pid)
{
	struct pty_pair *p = dev_tty_current_pty(NULL);

	(void)entry;
	(void)pid;
	if (p)
		return (p->m2s.count > 0 || !p->master_open) ? 1 : 0;
	return dev_tty_uses_console() ? devfs_console_can_read(entry, pid) : 0;
}

static int dev_tty_can_write(devfs_entry_t *entry, pid_t pid)
{
	struct pty_pair *p = dev_tty_current_pty(NULL);

	(void)entry;
	(void)pid;
	if (p)
		return p->s2m.count < PTY_BUF_SIZE ? 1 : 0;
	return dev_tty_uses_console() ? 1 : 0;
}

static const devfs_ops_t tty_ops = {
	.read = dev_tty_read,
	.write = dev_tty_write,
	.ioctl = dev_tty_ioctl,
	.open = dev_tty_open,
	.can_read = dev_tty_can_read,
	.can_write = dev_tty_can_write,
};
devfs_node_t dev_tty = {
    .entry = { .name = "tty", .mode = 0620, .device_id = 4, .rdev = IR0_MKDEV(5, 0) },
    .ops = &tty_ops,
    .ref_count = 0
};
