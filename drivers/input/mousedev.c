/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: mousedev.c
 * Description: PS/2 mouse multiplexer, Linux drivers/input/mousedev.c
 *              model: every pointer device (REL_X, REL_Y, BTN_LEFT) feeds
 *              /dev/input/mice, which reads as standard three-byte PS/2
 *              packets. /dev/mouse is the same node under its IR0 name.
 *              Each open has its own packet queue.
 */

#include <ir0/input.h>
#include <ir0/devfs.h>
#include <ir0/errno.h>
#include <ir0/io_async.h>
#include <ir0/kmem.h>
#include <ir0/poll.h>
#include <ir0/spinlock.h>
#include <ir0/types.h>

/* Internal devfs handles of /dev/input/mice (13, 63) and legacy /dev/mouse. */
#define MOUSEDEV_DEVICE_ID	0x121u
#define MOUSEDEV_LEGACY_ID	7u
#define MOUSEDEV_MAJOR		13
#define MOUSEDEV_MIX		63
#define MOUSEDEV_PACKETS	16u

struct mousedev_packet
{
	int dx;
	int dy;
	unsigned int buttons;
};

struct mousedev_client
{
	struct mousedev_client *next;
	int refs;
	unsigned int head;
	unsigned int tail;
	struct mousedev_packet packets[MOUSEDEV_PACKETS];
	/* The packet being read, byte by byte. */
	uint8_t ps2[3];
	unsigned int bufsiz;
	unsigned int pos;
};

/*
 * Guards the client list, every client queue and the motion gathered
 * since the last report. Taken inside the input core lock.
 */
static ir0_spinlock_t mousedev_lock;
static struct mousedev_client *mousedev_clients;
static struct mousedev_packet mousedev_pending;
static unsigned int mousedev_buttons;
static int mousedev_changed;

static int clamp127(int v)
{
	return v > 127 ? 127 : (v < -127 ? -127 : v);
}

/* Caller holds mousedev_lock. mousedev_notify_readers() */
static void mousedev_report(void)
{
	struct mousedev_client *c;

	mousedev_pending.buttons = mousedev_buttons;
	for (c = mousedev_clients; c; c = c->next)
	{
		unsigned int next = (c->head + 1u) % MOUSEDEV_PACKETS;

		/* Full: the newest state replaces the oldest packet. */
		if (next == c->tail)
			c->tail = (c->tail + 1u) % MOUSEDEV_PACKETS;
		c->packets[c->head] = mousedev_pending;
		c->head = next;
	}
	mousedev_pending.dx = 0;
	mousedev_pending.dy = 0;
}

static void mousedev_event(struct input_handle *handle, unsigned int type,
			   unsigned int code, int value)
{
	static const unsigned int btn_bits[] = {
		[BTN_LEFT - BTN_MOUSE] = 1u << 0,
		[BTN_RIGHT - BTN_MOUSE] = 1u << 1,
		[BTN_MIDDLE - BTN_MOUSE] = 1u << 2,
		[BTN_SIDE - BTN_MOUSE] = 1u << 3,
		[BTN_EXTRA - BTN_MOUSE] = 1u << 4,
	};
	unsigned long flags;
	int notify = 0;

	(void)handle;
	flags = ir0_spin_lock_irqsave(&mousedev_lock);
	switch (type)
	{
	case EV_REL:
		if (code == REL_X)
			mousedev_pending.dx += value;
		else if (code == REL_Y)
			/* PS/2 counts up as positive, evdev down. */
			mousedev_pending.dy -= value;
		mousedev_changed = 1;
		break;
	case EV_KEY:
		if (code >= BTN_LEFT && code <= BTN_EXTRA)
		{
			if (value)
				mousedev_buttons |= btn_bits[code - BTN_MOUSE];
			else
				mousedev_buttons &= ~btn_bits[code - BTN_MOUSE];
			mousedev_changed = 1;
		}
		break;
	case EV_SYN:
		if (code == SYN_REPORT && mousedev_changed)
		{
			mousedev_report();
			mousedev_changed = 0;
			notify = 1;
		}
		break;
	default:
		break;
	}
	ir0_spin_unlock_irqrestore(&mousedev_lock, flags);
	if (notify)
	{
		(void)poll_wake_check_nosched();
		io_async_notify_device(MOUSEDEV_DEVICE_ID);
		io_async_notify_device(MOUSEDEV_LEGACY_ID);
	}
}

/* mousedev_ids: a relative pointer with a left button. */
static int mousedev_connect(struct input_handler *handler,
			    struct input_dev *dev)
{
	struct input_handle *handle;

	if (!test_bit(EV_REL, dev->evbit) || !test_bit(REL_X, dev->relbit) ||
	    !test_bit(REL_Y, dev->relbit) || !test_bit(EV_KEY, dev->evbit) ||
	    !test_bit(BTN_LEFT, dev->keybit))
		return -ENODEV;
	handle = kmalloc(sizeof(*handle));
	if (!handle)
		return -ENOMEM;
	handle->dev = dev;
	handle->handler = handler;
	handle->name = "mousedev";
	if (input_register_handle(handle) != 0)
	{
		kfree(handle);
		return -ENOMEM;
	}
	return 0;
}

static void mousedev_disconnect(struct input_handle *handle)
{
	input_unregister_handle(handle);
	kfree(handle);
}

static struct input_handler mousedev_handler = {
	.name = "mousedev",
	.event = mousedev_event,
	.connect = mousedev_connect,
	.disconnect = mousedev_disconnect,
};

static int mousedev_open(devfs_node_t *node, int flags, void **priv)
{
	struct mousedev_client *c;
	unsigned long irq_flags;

	(void)node;
	(void)flags;
	c = kmalloc(sizeof(*c));
	if (!c)
		return -ENOMEM;
	c->refs = 1;
	irq_flags = ir0_spin_lock_irqsave(&mousedev_lock);
	c->next = mousedev_clients;
	mousedev_clients = c;
	ir0_spin_unlock_irqrestore(&mousedev_lock, irq_flags);
	*priv = c;
	return 0;
}

static void mousedev_get(void *priv)
{
	struct mousedev_client *c = priv;
	unsigned long flags;

	flags = ir0_spin_lock_irqsave(&mousedev_lock);
	c->refs++;
	ir0_spin_unlock_irqrestore(&mousedev_lock, flags);
}

static void mousedev_release(void *priv)
{
	struct mousedev_client *c = priv;
	struct mousedev_client **pp;
	unsigned long flags;
	int last;

	flags = ir0_spin_lock_irqsave(&mousedev_lock);
	last = --c->refs == 0;
	if (last)
	{
		for (pp = &mousedev_clients; *pp; pp = &(*pp)->next)
		{
			if (*pp == c)
			{
				*pp = c->next;
				break;
			}
		}
	}
	ir0_spin_unlock_irqrestore(&mousedev_lock, flags);
	if (last)
		kfree(c);
}

/* Caller holds mousedev_lock. mousedev_packet(), plain PS/2 protocol. */
static void mousedev_next_packet(struct mousedev_client *c)
{
	struct mousedev_packet *p = &c->packets[c->tail];
	int dx = clamp127(p->dx);
	int dy = clamp127(p->dy);

	c->ps2[0] = (uint8_t)(0x08 | ((dx < 0) << 4) | ((dy < 0) << 5) |
			      (p->buttons & 0x07));
	c->ps2[1] = (uint8_t)dx;
	c->ps2[2] = (uint8_t)dy;
	c->bufsiz = 3;
	c->pos = 0;
	c->tail = (c->tail + 1u) % MOUSEDEV_PACKETS;
}

static int64_t mousedev_read(void *priv, void *buf, size_t count, int nonblock)
{
	struct mousedev_client *c = priv;
	uint8_t *out = buf;
	unsigned long flags;
	size_t n = 0;

	(void)nonblock;
	flags = ir0_spin_lock_irqsave(&mousedev_lock);
	if (c->pos == c->bufsiz && c->head != c->tail)
		mousedev_next_packet(c);
	while (n < count && c->pos < c->bufsiz)
		out[n++] = c->ps2[c->pos++];
	ir0_spin_unlock_irqrestore(&mousedev_lock, flags);
	return n ? (int64_t)n : -EAGAIN;
}

/*
 * Linux KDrive probes a psaux-style node by writing PS/2 commands before
 * reading packets. The handlers own the devices; the commands are taken
 * and nothing is forwarded, so no ACK/ID bytes enter the packet stream.
 */
static int64_t mousedev_write(void *priv, const void *buf, size_t count)
{
	(void)priv;
	(void)buf;
	return (int64_t)count;
}

static int mousedev_poll(void *priv)
{
	struct mousedev_client *c = priv;
	unsigned long flags;
	int ready;

	flags = ir0_spin_lock_irqsave(&mousedev_lock);
	ready = c->pos < c->bufsiz || c->head != c->tail;
	ir0_spin_unlock_irqrestore(&mousedev_lock, flags);
	return ready;
}

static const struct devfs_file_ops mousedev_fops = {
	.open = mousedev_open,
	.get = mousedev_get,
	.release = mousedev_release,
	.read = mousedev_read,
	.write = mousedev_write,
	.poll = mousedev_poll,
};

static devfs_node_t mousedev_node = {
	.entry = { .name = "input/mice", .mode = 0660,
		   .device_id = MOUSEDEV_DEVICE_ID,
		   .rdev = IR0_MKDEV(MOUSEDEV_MAJOR, MOUSEDEV_MIX) },
	.fops = &mousedev_fops,
};

static devfs_node_t mousedev_legacy_node = {
	.entry = { .name = "mouse", .mode = 0660,
		   .device_id = MOUSEDEV_LEGACY_ID },
	.fops = &mousedev_fops,
};

int mousedev_init(void)
{
	int ret = devfs_register_node(&mousedev_node);

	if (ret == 0)
		ret = devfs_register_node(&mousedev_legacy_node);

	if (ret)
		return ret;
	return input_register_handler(&mousedev_handler);
}
