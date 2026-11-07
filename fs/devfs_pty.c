/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: fs/devfs_pty.c
 * Description: PTY multiplex (/dev/ptmx, /dev/pts/N) for devfs.
 */
#include "devfs_internal.h"

struct pty_pair g_ptys[DEVFS_PTY_MAX];
devfs_node_t dev_pts_nodes[DEVFS_PTY_MAX];
static char dev_pts_names[DEVFS_PTY_MAX][8];
static int g_ptmx_pending_slot = -1;
static pid_t g_ptmx_pending_pid;

static struct pty_pair *pty_slot(int idx)
{
	if (idx < 0 || idx >= DEVFS_PTY_MAX)
		return NULL;
	return &g_ptys[idx];
}

static struct pty_pair *pty_from_pts_entry(const devfs_entry_t *entry);
static void pty_hangup_fg(struct pty_pair *p);

static struct pty_pair *pty_from_pts_entry(const devfs_entry_t *entry)
{
	uint32_t id;

	if (!entry)
		return NULL;
	id = entry->device_id;
	if (!devfs_is_pts_device(id))
		return NULL;
	return pty_slot((int)(id - DEVFS_PTS0_DEVICE_ID));
}

static int pty_alloc_master(void)
{
	int i;

	for (i = 0; i < DEVFS_PTY_MAX; i++)
	{
		if (!g_ptys[i].master_open && g_ptys[i].slave_refs == 0)
			return i;
	}
	return -1;
}

void *devfs_pty_vfs_mark(int slot)
{
	if (slot < 0 || slot >= DEVFS_PTY_MAX)
		return NULL;
	return (void *)(uintptr_t)(DEVFS_PTY_VFS_TAG + (unsigned)slot);
}

int devfs_pty_vfs_slot(const void *vfs_file)
{
	uintptr_t v = (uintptr_t)vfs_file;

	if (v < DEVFS_PTY_VFS_TAG ||
	    v >= DEVFS_PTY_VFS_TAG + DEVFS_PTY_MAX)
		return -1;
	return (int)(v - DEVFS_PTY_VFS_TAG);
}

int devfs_pty_take_pending_master_slot(void)
{
	int slot = g_ptmx_pending_slot;
	pid_t pid = g_ptmx_pending_pid;

	g_ptmx_pending_slot = -1;
	g_ptmx_pending_pid = 0;
	if (slot < 0 || pid != devfs_current_pid())
		return -1;
	return slot;
}

void devfs_pty_abort_pending_master(void)
{
	struct pty_pair *p;

	if (g_ptmx_pending_slot < 0 ||
	    g_ptmx_pending_pid != devfs_current_pid())
		return;
	p = pty_slot(g_ptmx_pending_slot);
	if (p)
	{
		pty_hangup_fg(p);
		p->master_open = 0;
		p->master_refs = 0;
		p->locked = 0;
	}
	g_ptmx_pending_slot = -1;
	g_ptmx_pending_pid = 0;
}

void devfs_pty_master_release_vfs(const void *vfs_file)
{
	struct pty_pair *p;
	int slot = devfs_pty_vfs_slot(vfs_file);

	if (slot < 0)
		return;
	p = pty_slot(slot);
	if (!p || p->master_refs <= 0)
		return;
	p->master_refs--;
	if (p->master_refs > 0)
		return;
	pty_hangup_fg(p);
	p->master_open = 0;
	p->locked = 0;
	p->slave_open = p->slave_refs > 0;
}

void devfs_pty_master_dup_vfs(const void *vfs_file)
{
	struct pty_pair *p;
	int slot = devfs_pty_vfs_slot(vfs_file);

	if (slot < 0)
		return;
	p = pty_slot(slot);
	if (!p || p->master_refs <= 0)
		return;
	p->master_refs++;
}

void devfs_pty_slave_dup_device(uint32_t device_id)
{
	struct pty_pair *p;

	if (!devfs_is_pts_device(device_id))
		return;
	p = pty_slot((int)(device_id - DEVFS_PTS0_DEVICE_ID));
	if (!p || p->slave_refs <= 0)
		return;
	p->slave_refs++;
	p->slave_open = 1;
}

static void pty_termios_ensure(struct pty_pair *p)
{
	if (!p || p->termios_ready)
		return;
	memset(&p->termios, 0, sizeof(p->termios));
	p->termios.c_iflag = IR0_CONSOLE_IFLAG_DEFAULT;
	p->termios.c_oflag = IR0_CONSOLE_OFLAG_DEFAULT;
	p->termios.c_cflag = IR0_CONSOLE_CFLAG_DEFAULT;
	p->termios.c_lflag = IR0_CONSOLE_LFLAG_DEFAULT;
	p->termios.c_cc[IR0_CC_VINTR] = 3;
	p->termios.c_cc[IR0_CC_VQUIT] = 28;
	p->termios.c_cc[IR0_CC_VERASE] = 127;
	p->termios.c_cc[IR0_CC_VEOF] = 4;
	p->termios.c_cc[IR0_CC_VMIN] = 1;
	p->termios_ready = 1;
}

static void pty_hangup_fg(struct pty_pair *p)
{
	if (!p || !p->ctty_set || p->fg_pgid <= 0)
		return;
	klog_print("PTY_SIGHUP_PGRP\n");
	(void)send_signal_pgrp(p->fg_pgid, SIGHUP);
	p->ctty_set = 0;
	p->fg_pgid = 0;
	p->session_sid = 0;
}

static int pty_ring_push(struct pty_ring *r, const char *src, size_t n)
{
	unsigned long irq_flags;
	size_t i;

	if (!r || !src || n == 0)
		return 0;

	/*
	 * Same race as pipe_write: count++ is a read-modify-write. Master and
	 * slave are different processes; preemption between load and store can
	 * drop bytes or inflate count past what is actually queued.
	 */
	irq_flags = irq_save();
	for (i = 0; i < n; i++)
	{
		if (r->count >= PTY_BUF_SIZE)
			break;
		r->buf[r->head] = src[i];
		r->head = (r->head + 1) % PTY_BUF_SIZE;
		r->count++;
	}
	irq_restore(irq_flags);
	return (int)i;
}

static int pty_ring_push_all(struct pty_ring *r, const char *src, size_t n)
{
	unsigned long irq_flags;
	size_t i;

	if (!r || !src || n == 0 || n > PTY_BUF_SIZE)
		return 0;
	irq_flags = irq_save();
	if (PTY_BUF_SIZE - r->count < n)
	{
		irq_restore(irq_flags);
		return 0;
	}
	for (i = 0; i < n; i++)
	{
		r->buf[r->head] = src[i];
		r->head = (r->head + 1) % PTY_BUF_SIZE;
		r->count++;
	}
	irq_restore(irq_flags);
	return 1;
}

int pty_ring_pop(struct pty_ring *r, char *dst, size_t n)
{
	unsigned long irq_flags;
	size_t i;

	if (!r || !dst || n == 0)
		return 0;

	irq_flags = irq_save();
	for (i = 0; i < n; i++)
	{
		if (r->count == 0)
			break;
		dst[i] = r->buf[r->tail];
		r->tail = (r->tail + 1) % PTY_BUF_SIZE;
		r->count--;
	}
	irq_restore(irq_flags);
	return (int)i;
}

static int pty_isig_on(const struct pty_pair *p)
{
	return p && (p->termios.c_lflag & IR0_LFLAG_ISIG) != 0;
}

static void pty_deliver_sig(struct pty_pair *p, int sig)
{
	if (p && p->ctty_set && p->fg_pgid > 0)
		(void)send_signal_pgrp(p->fg_pgid, sig);
}

static int pty_handle_isig(struct pty_pair *p, unsigned char c)
{
	unsigned char vintr;
	unsigned char vquit;

	if (!pty_isig_on(p))
		return 0;
	vintr = p->termios.c_cc[IR0_CC_VINTR];
	vquit = p->termios.c_cc[IR0_CC_VQUIT];
	if (vintr == 0)
		vintr = 3;
	if (vquit == 0)
		vquit = 28;
	if (c == vintr)
	{
		p->m2s.head = p->m2s.tail = p->m2s.count = 0;
		pty_deliver_sig(p, SIGINT);
		return 1;
	}
	if (c == vquit)
	{
		p->m2s.head = p->m2s.tail = p->m2s.count = 0;
		pty_deliver_sig(p, SIGQUIT);
		return 1;
	}
	return 0;
}

static int64_t pty_master_write_pair(struct pty_pair *p, const void *buf,
				     size_t count)
{
	const unsigned char *src = (const unsigned char *)buf;
	size_t consumed = 0;

	if (!p || !buf)
		return -EFAULT;
	pty_termios_ensure(p);
	while (consumed < count)
	{
		unsigned char c = src[consumed];
		char translated;

		if (c == '\r')
		{
			if (p->termios.c_iflag & IR0_IFLAG_IGNCR)
			{
				consumed++;
				continue;
			}
			if (p->termios.c_iflag & IR0_IFLAG_ICRNL)
				c = '\n';
		}
		else if (c == '\n' && (p->termios.c_iflag & IR0_IFLAG_INLCR))
			c = '\r';
		if (pty_handle_isig(p, (unsigned char)c))
		{
			consumed++;
			continue;
		}
		translated = (char)c;
		if (pty_ring_push(&p->m2s, &translated, 1) != 1)
			break;
		consumed++;
	}
	return (int64_t)consumed;
}

int64_t devfs_pty_master_read(const void *vfs_file, void *buf, size_t count)
{
	struct pty_pair *p;
	int slot = devfs_pty_vfs_slot(vfs_file);

	if (slot < 0 || !buf)
		return -EFAULT;
	p = pty_slot(slot);
	if (!p)
		return -EBADF;
	return pty_ring_pop(&p->s2m, (char *)buf, count);
}

int64_t devfs_pty_master_write(const void *vfs_file, const void *buf,
			       size_t count)
{
	int slot = devfs_pty_vfs_slot(vfs_file);

	if (slot < 0)
		return -EBADF;
	return pty_master_write_pair(pty_slot(slot), buf, count);
}

int64_t pty_master_ioctl_pair(struct pty_pair *p, int slot,
				    uint64_t request, void *arg)
{
	if (!p)
		return -EBADF;

	if (request == IR0_CONSOLE_TIOCGWINSZ)
	{
		struct ir0_winsize win;

		if (!arg)
			return -EINVAL;
		if (p->winsz.ws_row != 0 || p->winsz.ws_col != 0)
			win = p->winsz;
		else
			return ir0_console_ioctl_winsize(arg);
		if (copy_to_user(arg, &win, sizeof(win)) != 0)
			return -EFAULT;
		return 0;
	}
	if (request == IR0_CONSOLE_TIOCSWINSZ)
	{
		struct ir0_winsize win;

		if (!arg)
			return -EINVAL;
		if (copy_from_user(&win, arg, sizeof(win)) != 0)
			return -EFAULT;
		if (win.ws_row == 0 || win.ws_col == 0)
			return -EINVAL;
		if (memcmp(&p->winsz, &win, sizeof(win)) == 0)
			return 0;
		p->winsz = win;
		if (p->ctty_set && p->fg_pgid > 0)
		{
			klog_print("PTY_WINCH_SENT\n");
			(void)send_signal_pgrp(p->fg_pgid, SIGWINCH);
		}
		return 0;
	}
	if (request == IR0_TIOCGPTN)
	{
		unsigned int n = (unsigned int)slot;

		if (!arg)
			return -EINVAL;
		if (copy_to_user(arg, &n, sizeof(n)) != 0)
			return -EFAULT;
		return 0;
	}
	if (request == IR0_TIOCSPTLCK)
	{
		int lock = 0;

		if (!arg)
			return -EINVAL;
		if (copy_from_user(&lock, arg, sizeof(lock)) != 0)
			return -EFAULT;
		p->locked = lock ? 1 : 0;
		return 0;
	}
	if (request == IR0_TIOCSCTTY)
	{
		if (!current_process)
			return -ESRCH;
		if (current_process->sid != current_process->task.pid)
			return -EPERM;
		p->ctty_set = 1;
		p->session_sid = current_process->sid;
		p->fg_pgid = current_process->pgid;
		klog_print("PTY_TIOCSCTTY_OK\n");
		return 0;
	}
	if (request == IR0_TIOCSPGRP)
	{
		pid_t pg;

		if (!arg)
			return -EINVAL;
		if (copy_from_user(&pg, arg, sizeof(pg)) != 0)
			return -EFAULT;
		if (pg <= 0)
			return -EINVAL;
		if (!p->ctty_set)
			return -ENOTTY;
		p->fg_pgid = pg;
		return 0;
	}
	if (request == IR0_TIOCGPGRP)
	{
		pid_t pg;

		if (!arg)
			return -EINVAL;
		if (!p->ctty_set)
			return -ENOTTY;
		pg = p->fg_pgid;
		if (copy_to_user(arg, &pg, sizeof(pg)) != 0)
			return -EFAULT;
		return 0;
	}
	if (request == IR0_CONSOLE_TCGETS)
	{
		pty_termios_ensure(p);
		if (!arg)
			return -EINVAL;
		return copy_to_user(arg, &p->termios, sizeof(p->termios)) == 0
			? 0 : -EFAULT;
	}
	if (request == IR0_CONSOLE_TCSETS ||
	    request == IR0_CONSOLE_TCSETSW || request == IR0_CONSOLE_TCSETSF)
	{
		struct ir0_termios termios;

		if (!arg)
			return -EINVAL;
		if (copy_from_user(&termios, arg, sizeof(termios)) != 0)
			return -EFAULT;
		p->termios = termios;
		p->termios_ready = 1;
		if (request == IR0_CONSOLE_TCSETSF)
			p->m2s.head = p->m2s.tail = p->m2s.count = 0;
		return 0;
	}
	return -ENOTTY;
}

int64_t devfs_pty_master_ioctl(const void *vfs_file, uint64_t request,
			       void *arg)
{
	int slot = devfs_pty_vfs_slot(vfs_file);

	if (slot < 0)
		return -EBADF;
	return pty_master_ioctl_pair(pty_slot(slot), slot, request, arg);
}

int devfs_pty_master_can_read(const void *vfs_file)
{
	struct pty_pair *p;
	int slot = devfs_pty_vfs_slot(vfs_file);

	if (slot < 0)
		return 0;
	p = pty_slot(slot);
	if (!p)
		return 0;
	return (p->s2m.count > 0 || !p->slave_open) ? 1 : 0;
}

int devfs_pty_master_can_write(const void *vfs_file)
{
	struct pty_pair *p;
	int slot = devfs_pty_vfs_slot(vfs_file);

	if (slot < 0)
		return 0;
	p = pty_slot(slot);
	if (!p)
		return 0;
	return p->m2s.count < PTY_BUF_SIZE ? 1 : 0;
}

static int64_t dev_ptmx_open(devfs_entry_t *entry, int flags)
{
	int slot;
	struct pty_pair *p;

	(void)entry;
	(void)flags;
	slot = pty_alloc_master();
	if (slot < 0)
		return -EBUSY;
	p = pty_slot(slot);
	p->master_open = 1;
	p->master_refs = 1;
	p->locked = 1;
	p->slave_open = 0;
	p->slave_refs = 0;
	p->m2s.head = p->m2s.tail = p->m2s.count = 0;
	p->s2m.head = p->s2m.tail = p->s2m.count = 0;
	dev_pts_nodes[slot].entry.uid = 0;
	dev_pts_nodes[slot].entry.gid = 0;
	dev_pts_nodes[slot].entry.mode = 0620;
	pty_termios_ensure(p);
	g_ptmx_pending_slot = slot;
	g_ptmx_pending_pid = devfs_current_pid();
	return 0;
}

static int64_t dev_pts_open(devfs_entry_t *entry, int flags)
{
	struct pty_pair *p = pty_from_pts_entry(entry);

	(void)flags;
	if (!p)
		return -ENODEV;
	if (p->locked)
		return -EIO;
	if (!p->master_open)
		return -EIO;
	p->slave_open = 1;
	p->slave_refs++;
	return 0;
}

static int64_t dev_ptmx_close(devfs_entry_t *entry)
{
	(void)entry;
	return 0;
}

static int64_t dev_pts_close(devfs_entry_t *entry)
{
	struct pty_pair *p = pty_from_pts_entry(entry);

	if (!p)
		return 0;
	if (p->slave_refs > 0)
		p->slave_refs--;
	p->slave_open = p->slave_refs > 0;
	return 0;
}

static int64_t dev_ptmx_read(devfs_entry_t *entry, void *buf, size_t count,
			     off_t offset)
{
	(void)entry;
	(void)buf;
	(void)count;
	(void)offset;
	return -EBADF;
}

static int64_t dev_ptmx_write(devfs_entry_t *entry, const void *buf,
			      size_t count, off_t offset)
{
	(void)entry;
	(void)buf;
	(void)count;
	(void)offset;
	return -EBADF;
}

static int64_t dev_pts_read(devfs_entry_t *entry, void *buf, size_t count,
			    off_t offset)
{
	struct pty_pair *p = pty_from_pts_entry(entry);

	(void)offset;
	if (!buf)
		return -EFAULT;
	if (!p)
		return -EBADF;
	return pty_ring_pop(&p->m2s, (char *)buf, count);
}

int64_t dev_pts_write(devfs_entry_t *entry, const void *buf,
			     size_t count, off_t offset)
{
	struct pty_pair *p = pty_from_pts_entry(entry);
	const unsigned char *src = (const unsigned char *)buf;
	size_t consumed = 0;

	(void)offset;
	if (!p || !buf)
		return -EFAULT;
	pty_termios_ensure(p);
	while (consumed < count)
	{
		char c = (char)src[consumed];

		if (c == '\n' &&
		    (p->termios.c_oflag &
		     (IR0_OFLAG_OPOST | IR0_OFLAG_ONLCR)) ==
		    (IR0_OFLAG_OPOST | IR0_OFLAG_ONLCR))
		{
			static const char crlf[2] = { '\r', '\n' };

			if (!pty_ring_push_all(&p->s2m, crlf, sizeof(crlf)))
				break;
		}
		else if (pty_ring_push(&p->s2m, &c, 1) != 1)
			break;
		consumed++;
	}
	return (int64_t)consumed;
}

static int64_t dev_pty_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
	struct pty_pair *p = pty_from_pts_entry(entry);
	int slot;

	if (!p)
		return -ENOTTY;
	slot = (int)(entry->device_id - DEVFS_PTS0_DEVICE_ID);
	return pty_master_ioctl_pair(p, slot, request, arg);
}

static int dev_ptmx_can_read(devfs_entry_t *entry, pid_t pid)
{
	(void)entry;
	(void)pid;
	return 1;
}

static int dev_ptmx_can_write(devfs_entry_t *entry, pid_t pid)
{
	(void)entry;
	(void)pid;
	return 1;
}

static int dev_pts_can_read(devfs_entry_t *entry, pid_t pid)
{
	struct pty_pair *p = pty_from_pts_entry(entry);

	(void)pid;
	if (!p)
		return 0;
	return (p->m2s.count > 0 || !p->master_open) ? 1 : 0;
}

static int dev_pts_can_write(devfs_entry_t *entry, pid_t pid)
{
	struct pty_pair *p = pty_from_pts_entry(entry);

	(void)pid;
	if (!p)
		return 0;
	return p->s2m.count < PTY_BUF_SIZE ? 1 : 0;
}

static const devfs_ops_t ptmx_ops = {
	.read = dev_ptmx_read,
	.write = dev_ptmx_write,
	.ioctl = NULL,
	.open = dev_ptmx_open,
	.close = dev_ptmx_close,
	.can_read = dev_ptmx_can_read,
	.can_write = dev_ptmx_can_write,
};

static const devfs_ops_t pts_ops = {
	.read = dev_pts_read,
	.write = dev_pts_write,
	.ioctl = dev_pty_ioctl,
	.open = dev_pts_open,
	.close = dev_pts_close,
	.can_read = dev_pts_can_read,
	.can_write = dev_pts_can_write,
};
devfs_node_t dev_ptmx = {
	.entry = { .name = "ptmx", .mode = 0666,
		   .device_id = DEVFS_PTMX_DEVICE_ID },
	.ops = &ptmx_ops,
	.ref_count = 0,
};

void devfs_pty_register_nodes(void)
{
	int i;

	for (i = 0; i < DEVFS_PTY_MAX; i++)
	{
		snprintf(dev_pts_names[i], sizeof(dev_pts_names[i]), "pts/%d", i);
		dev_pts_nodes[i].entry.name = dev_pts_names[i];
		dev_pts_nodes[i].entry.mode = 0620;
		dev_pts_nodes[i].entry.device_id = DEVFS_PTS0_DEVICE_ID + (uint32_t)i;
		dev_pts_nodes[i].ops = &pts_ops;
		dev_pts_nodes[i].ref_count = 0;
		if (devfs_register_node(&dev_pts_nodes[i]) != 0)
			klog_print("DEVFS_PTS_REGISTER_FAIL\n");
	}
}
