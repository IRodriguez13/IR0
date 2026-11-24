/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: evdev.c
 * Description: Event device handler, Linux drivers/input/evdev.c model.
 *              Every input device gets /dev/input/eventN; every open of it
 *              gets its own client ring, clock and grab state. The legacy
 *              IR0 node /dev/events0 is one more evdev without a device: it
 *              receives the events of every device.
 */

#include <ir0/input.h>
#include <ir0/clock.h>
#include <ir0/console.h>
#include <ir0/copy_user.h>
#include <ir0/devfs.h>
#include <ir0/errno.h>
#include <ir0/input_backend.h>
#include <ir0/kmem.h>
#include <ir0/poll.h>
#include <ir0/spinlock.h>
#include <ir0/types.h>
#include <config.h>
#include <string.h>

#define EVDEV_MINORS		32
#define EVDEV_MINOR_BASE	64
#define EVDEV_INPUT_MAJOR	13
/* Internal devfs handles of /dev/input/event0..31. */
#define EVDEV_DEVICE_ID_BASE	0x100u
/* Legacy /dev/events0. */
#define EVDEV_EVENTS0_ID	16u
/* EVDEV_MIN_BUFFER_SIZE: a power of two. */
#define EVDEV_BUFFER_SIZE	64u

#define EV_CLK_REAL	0
#define EV_CLK_MONO	1
#define EV_CLK_BOOT	2

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME	0
#endif
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC	1
#endif
#ifndef CLOCK_BOOTTIME
#define CLOCK_BOOTTIME	7
#endif

struct evdev;

struct evdev_client
{
	struct evdev *evdev;
	struct evdev_client *next;
	int refs;
	int revoked;
	int clk_type;
	unsigned int head;
	unsigned int tail;
	/* Events before packet_head belong to complete packets. */
	unsigned int packet_head;
	struct input_event buffer[EVDEV_BUFFER_SIZE];
};

struct evdev
{
	devfs_node_t node;
	char name[16];
	struct input_handle handle;
	struct evdev_client *clients;
	struct evdev_client *grab;
	/* One for the device while connected, one per client. */
	int refs;
	int exist;
	int minor;
	/* The legacy multiplexer: no device of its own. */
	int is_mux;
};

/*
 * Guards evdev_table, every evdev's client list, grab, refs and exist,
 * and every client ring. Taken inside the input core lock when an event
 * arrives, so it is never held across a call into the input core.
 */
static ir0_spinlock_t evdev_lock;
static struct evdev *evdev_table[EVDEV_MINORS];
static struct evdev evdev_mux;
static int events0_readers;

static const struct devfs_file_ops evdev_fops;

static void evdev_put(struct evdev *evdev)
{
	unsigned long flags;
	int free_it;

	flags = ir0_spin_lock_irqsave(&evdev_lock);
	free_it = (--evdev->refs == 0) && !evdev->is_mux;
	ir0_spin_unlock_irqrestore(&evdev_lock, flags);
	if (free_it)
	{
		kfree(evdev);
	}
}

static void evdev_stamp(struct input_event *ev, int clk_type)
{
	uint64_t ms = clock_get_uptime_milliseconds();

	if (clk_type == EV_CLK_REAL && clock_realtime_available())
	{
		/* Same sources as gettimeofday(2). */
		ev->time.tv_sec = clock_get_current_time();
	}
	else
	{
		ev->time.tv_sec = (time_t)(ms / 1000u);
	}
	ev->time.tv_usec = (suseconds_t)((ms % 1000u) * 1000u);
}

/* Caller holds evdev_lock. __pass_event() */
static void client_pass_event(struct evdev_client *c,
			      const struct input_event *event)
{
	const unsigned int mask = EVDEV_BUFFER_SIZE - 1u;

	c->buffer[c->head++] = *event;
	c->head &= mask;
	if (c->head == c->tail)
	{
		/*
		 * Full: drop every unconsumed event, leaving SYN_DROPPED and
		 * the newest event.
		 */
		c->tail = (c->head - 2u) & mask;
		c->buffer[c->tail].time = event->time;
		c->buffer[c->tail].type = EV_SYN;
		c->buffer[c->tail].code = SYN_DROPPED;
		c->buffer[c->tail].value = 0;
		c->packet_head = c->tail;
	}
	if (event->type == EV_SYN && event->code == SYN_REPORT)
	{
		c->packet_head = c->head;
	}
}

/* Caller holds evdev_lock. __evdev_queue_syn_dropped() */
static void client_queue_syn_dropped(struct evdev_client *c)
{
	const unsigned int mask = EVDEV_BUFFER_SIZE - 1u;
	struct input_event ev;

	evdev_stamp(&ev, c->clk_type);
	ev.type = EV_SYN;
	ev.code = SYN_DROPPED;
	ev.value = 0;
	c->buffer[c->head++] = ev;
	c->head &= mask;
	if (c->head == c->tail)
	{
		c->tail = (c->head - 1u) & mask;
		c->packet_head = c->tail;
	}
}

/*
 * Caller holds evdev_lock. __evdev_flush_queue(): drop the queued events
 * of @type and the reports left empty, keep the rest in order.
 */
static void client_flush_type(struct evdev_client *c, unsigned int type)
{
	const unsigned int mask = EVDEV_BUFFER_SIZE - 1u;
	unsigned int i;
	unsigned int head = c->tail;
	unsigned int num = 1;

	c->packet_head = c->tail;
	for (i = c->tail; i != c->head; i = (i + 1u) & mask)
	{
		struct input_event *ev = &c->buffer[i];
		int is_report = ev->type == EV_SYN && ev->code == SYN_REPORT;

		if (ev->type == type)
		{
			continue;
		}
		if (is_report && num == 0)
		{
			continue;
		}
		if (head != i)
		{
			c->buffer[head] = *ev;
		}
		num++;
		head = (head + 1u) & mask;
		if (is_report)
		{
			num = 0;
			c->packet_head = head;
		}
	}
	c->head = head;
}

/* Caller holds evdev_lock. */
static void evdev_pass(struct evdev *evdev, unsigned int type,
		       unsigned int code, int value)
{
	struct input_event ev;
	struct evdev_client *c;

	ev.type = (uint16_t)type;
	ev.code = (uint16_t)code;
	ev.value = value;
	if (evdev->grab)
	{
		evdev_stamp(&ev, evdev->grab->clk_type);
		client_pass_event(evdev->grab, &ev);
		return;
	}
	for (c = evdev->clients; c; c = c->next)
	{
		if (c->revoked)
		{
			continue;
		}
		evdev_stamp(&ev, c->clk_type);
		client_pass_event(c, &ev);
	}
}

static void evdev_event(struct input_handle *handle, unsigned int type,
			unsigned int code, int value)
{
	struct evdev *evdev = handle->private;
	unsigned long flags;

	flags = ir0_spin_lock_irqsave(&evdev_lock);
	evdev_pass(evdev, type, code, value);
	evdev_pass(&evdev_mux, type, code, value);
	ir0_spin_unlock_irqrestore(&evdev_lock, flags);
	if (type == EV_SYN && code == SYN_REPORT)
	{
		input_event_wake_readers();
	}
}

static int evdev_connect(struct input_handler *handler, struct input_dev *dev)
{
	struct evdev *evdev;
	unsigned long flags;
	int minor;

	evdev = kmalloc(sizeof(*evdev));
	if (!evdev)
	{
		return -ENOMEM;
	}
	flags = ir0_spin_lock_irqsave(&evdev_lock);
	for (minor = 0; minor < EVDEV_MINORS; minor++)
	{
		if (!evdev_table[minor])
		{
			evdev_table[minor] = evdev;
			break;
		}
	}
	ir0_spin_unlock_irqrestore(&evdev_lock, flags);
	if (minor == EVDEV_MINORS)
	{
		kfree(evdev);
		return -ENFILE;
	}

	evdev->minor = minor;
	evdev->exist = 1;
	evdev->refs = 1;
	snprintf(evdev->name, sizeof(evdev->name), "input/event%d", minor);
	evdev->node.entry.name = evdev->name;
	evdev->node.entry.mode = 0660;
	evdev->node.entry.device_id = EVDEV_DEVICE_ID_BASE + (uint32_t)minor;
	evdev->node.entry.rdev = IR0_MKDEV(EVDEV_INPUT_MAJOR,
					   EVDEV_MINOR_BASE + minor);
	evdev->node.entry.driver_data = evdev;
	evdev->node.fops = &evdev_fops;
	evdev->handle.private = evdev;
	evdev->handle.name = evdev->name;
	evdev->handle.dev = dev;
	evdev->handle.handler = handler;

	if (input_register_handle(&evdev->handle) != 0 ||
	    devfs_register_node(&evdev->node) != 0)
	{
		input_unregister_handle(&evdev->handle);
		flags = ir0_spin_lock_irqsave(&evdev_lock);
		evdev_table[minor] = NULL;
		ir0_spin_unlock_irqrestore(&evdev_lock, flags);
		kfree(evdev);
		return -ENOMEM;
	}
	return 0;
}

static void evdev_disconnect(struct input_handle *handle)
{
	struct evdev *evdev = handle->private;
	unsigned long flags;

	devfs_unregister_node(&evdev->node);
	input_unregister_handle(handle);
	flags = ir0_spin_lock_irqsave(&evdev_lock);
	evdev->exist = 0;
	evdev->grab = NULL;
	evdev_table[evdev->minor] = NULL;
	ir0_spin_unlock_irqrestore(&evdev_lock, flags);
	/* Readers wake to -ENODEV. */
	input_event_wake_readers();
	evdev_put(evdev);
}

static struct input_handler evdev_handler = {
	.name = "evdev",
	.event = evdev_event,
	.connect = evdev_connect,
	.disconnect = evdev_disconnect,
};

static int evdev_open(devfs_node_t *node, int flags, void **priv)
{
	struct evdev *evdev = node->entry.driver_data;
	struct evdev_client *c;
	unsigned long irq_flags;
	int ret = 0;

	(void)flags;
	if (!evdev)
	{
		return -ENODEV;
	}
	c = kmalloc(sizeof(*c));
	if (!c)
	{
		return -ENOMEM;
	}
	c->evdev = evdev;
	c->refs = 1;
	c->clk_type = EV_CLK_REAL;
	irq_flags = ir0_spin_lock_irqsave(&evdev_lock);
	if (!evdev->exist)
	{
		ret = -ENODEV;
	}
	else
	{
		c->next = evdev->clients;
		evdev->clients = c;
		evdev->refs++;
		if (evdev->is_mux)
		{
			events0_readers++;
		}
	}
	ir0_spin_unlock_irqrestore(&evdev_lock, irq_flags);
	if (ret)
	{
		kfree(c);
		return ret;
	}
	*priv = c;
	return 0;
}

static void evdev_get(void *priv)
{
	struct evdev_client *c = priv;
	unsigned long flags;

	if (!c)
	{
		return;
	}
	flags = ir0_spin_lock_irqsave(&evdev_lock);
	c->refs++;
	ir0_spin_unlock_irqrestore(&evdev_lock, flags);
}

/* evdev_ungrab(): only the grabbing client may release. */
static int evdev_ungrab(struct evdev_client *c)
{
	struct evdev *evdev = c->evdev;
	unsigned long flags;
	int was;

	flags = ir0_spin_lock_irqsave(&evdev_lock);
	was = evdev->grab == c;
	if (was)
	{
		evdev->grab = NULL;
	}
	ir0_spin_unlock_irqrestore(&evdev_lock, flags);
	if (!was)
	{
		return -EINVAL;
	}
	input_release_device(&evdev->handle);
	return 0;
}

static int evdev_grab(struct evdev_client *c)
{
	struct evdev *evdev = c->evdev;
	unsigned long flags;
	int ret;

	flags = ir0_spin_lock_irqsave(&evdev_lock);
	ret = evdev->grab ? -EBUSY : 0;
	ir0_spin_unlock_irqrestore(&evdev_lock, flags);
	if (ret)
	{
		return ret;
	}
	ret = input_grab_device(&evdev->handle);
	if (ret)
	{
		return ret;
	}
	flags = ir0_spin_lock_irqsave(&evdev_lock);
	if (evdev->grab || !evdev->exist)
	{
		ret = evdev->exist ? -EBUSY : -ENODEV;
	}
	else
	{
		evdev->grab = c;
	}
	ir0_spin_unlock_irqrestore(&evdev_lock, flags);
	if (ret)
	{
		input_release_device(&evdev->handle);
	}
	return ret;
}

static void evdev_release(void *priv)
{
	struct evdev_client *c = priv;
	struct evdev *evdev;
	struct evdev_client **pp;
	unsigned long flags;
	int last;
	int mux_idle = 0;

	if (!c)
	{
		return;
	}
	evdev = c->evdev;
	flags = ir0_spin_lock_irqsave(&evdev_lock);
	last = --c->refs == 0;
	ir0_spin_unlock_irqrestore(&evdev_lock, flags);
	if (!last)
	{
		return;
	}
	(void)evdev_ungrab(c);
	flags = ir0_spin_lock_irqsave(&evdev_lock);
	for (pp = &evdev->clients; *pp; pp = &(*pp)->next)
	{
		if (*pp == c)
		{
			*pp = c->next;
			break;
		}
	}
	if (evdev->is_mux && events0_readers > 0)
	{
		mux_idle = --events0_readers == 0;
	}
	ir0_spin_unlock_irqrestore(&evdev_lock, flags);
	kfree(c);
	if (mux_idle)
	{
		/*
		 * Last /dev/events0 reader: drop cooked LD state and any raw
		 * ASCII that may have landed while divert was ending.
		 */
		ir0_console_flush_input();
		input_kbd_clear();
	}
	evdev_put(evdev);
}

static int evdev_poll(void *priv)
{
	struct evdev_client *c = priv;
	unsigned long flags;
	int ready;

	if (!c)
	{
		return 0;
	}
	flags = ir0_spin_lock_irqsave(&evdev_lock);
	ready = !c->evdev->exist || c->revoked || c->packet_head != c->tail;
	ir0_spin_unlock_irqrestore(&evdev_lock, flags);
	return ready;
}

/* evdev_read(): whole events of complete packets only. */
static int64_t evdev_read(void *priv, void *buf, size_t count, int nonblock)
{
	struct evdev_client *c = priv;
	struct input_event *out = buf;
	unsigned long flags;
	size_t n = 0;
	size_t max = count / sizeof(struct input_event);
	int64_t ret;

	if (!c)
	{
		return -EBADF;
	}
	if (count != 0 && count < sizeof(struct input_event))
	{
		return -EINVAL;
	}
	flags = ir0_spin_lock_irqsave(&evdev_lock);
	if (!c->evdev->exist || c->revoked)
	{
		ret = -ENODEV;
	}
	else if (c->packet_head == c->tail)
	{
		ret = (count == 0 && !nonblock) ? 0 : -EAGAIN;
	}
	else
	{
		while (n < max && c->packet_head != c->tail)
		{
			out[n++] = c->buffer[c->tail];
			c->tail = (c->tail + 1u) & (EVDEV_BUFFER_SIZE - 1u);
		}
		ret = (int64_t)(n * sizeof(struct input_event));
	}
	ir0_spin_unlock_irqrestore(&evdev_lock, flags);
	return ret;
}

/* The device behind the mux that can take @type: first registered match. */
static struct evdev *mux_target(unsigned int type);

/* evdev_write(): each whole event is injected into the device. */
static int64_t evdev_write(void *priv, const void *buf, size_t count)
{
	struct evdev_client *c = priv;
	const struct input_event *in = buf;
	size_t i;

	if (!c)
	{
		return -EBADF;
	}
	if (count != 0 && count < sizeof(struct input_event))
	{
		return -EINVAL;
	}
	if (!c->evdev->exist || c->revoked || c->evdev->is_mux)
	{
		return c->evdev->is_mux ? -EINVAL : -ENODEV;
	}
	for (i = 0; i < count / sizeof(struct input_event); i++)
	{
		input_inject_event(&c->evdev->handle, in[i].type, in[i].code,
				   in[i].value);
	}
	return (int64_t)(i * sizeof(struct input_event));
}

/* A copy of what one ioctl needs, taken under the input lock. */
enum evdev_query_kind
{
	EVQ_ID,
	EVQ_STR,
	EVQ_BITS,
	EVQ_STATE,
	EVQ_PROP,
	EVQ_ABS,
	EVQ_SET_ABS,
	EVQ_REP,
};

#define EVQ_STR_MAX 128

struct evdev_query
{
	enum evdev_query_kind kind;
	unsigned int which;
	int ret;
	size_t len;
	struct input_id id;
	struct input_absinfo abs;
	unsigned long bits[BITS_TO_LONGS(KEY_CNT)];
	char str[EVQ_STR_MAX];
};

static int query_bits(struct input_dev *dev, unsigned int type,
		      const unsigned long **bits, unsigned int *max)
{
	switch (type)
	{
	case 0:
		*bits = dev->evbit;
		*max = EV_MAX;
		return 0;
	case EV_KEY:
		*bits = dev->keybit;
		*max = KEY_MAX;
		return 0;
	case EV_REL:
		*bits = dev->relbit;
		*max = REL_MAX;
		return 0;
	case EV_ABS:
		*bits = dev->absbit;
		*max = ABS_MAX;
		return 0;
	case EV_MSC:
		*bits = dev->mscbit;
		*max = MSC_MAX;
		return 0;
	case EV_LED:
		*bits = dev->ledbit;
		*max = LED_MAX;
		return 0;
	case EV_SND:
		*bits = dev->sndbit;
		*max = SND_MAX;
		return 0;
	case EV_SW:
		*bits = dev->swbit;
		*max = SW_MAX;
		return 0;
	case EV_FF:
		/* No force feedback: an empty map of FF_MAX bits. */
		*bits = NULL;
		*max = FF_MAX;
		return 0;
	default:
		return -EINVAL;
	}
}

static int query_state(struct input_dev *dev, unsigned int type,
		       const unsigned long **bits, unsigned int *max)
{
	switch (type)
	{
	case EV_KEY:
		*bits = dev->key;
		*max = KEY_MAX;
		return 0;
	case EV_LED:
		*bits = dev->led;
		*max = LED_MAX;
		return 0;
	case EV_SND:
		*bits = dev->snd;
		*max = SND_MAX;
		return 0;
	case EV_SW:
		*bits = dev->sw;
		*max = SW_MAX;
		return 0;
	default:
		return -EINVAL;
	}
}

/* OR @bits (or nothing) into q->bits; len is that of bits_to_user(). */
static void query_merge(struct evdev_query *q, const unsigned long *bits,
			unsigned int max)
{
	size_t i;
	size_t nlongs = BITS_TO_LONGS(max);

	q->len = nlongs * sizeof(long);
	for (i = 0; bits && i < nlongs; i++)
	{
		q->bits[i] |= bits[i];
	}
}

static void query_fill(struct input_dev *dev, void *arg)
{
	struct evdev_query *q = arg;
	const unsigned long *bits = NULL;
	unsigned int max = 0;
	const char *s;

	switch (q->kind)
	{
	case EVQ_ID:
		q->id = dev->id;
		break;
	case EVQ_STR:
		s = q->which == 0 ? dev->name :
		    q->which == 1 ? dev->phys : dev->uniq;
		if (!s)
		{
			q->ret = -ENOENT;
			break;
		}
		q->len = strlen(s) + 1u;
		if (q->len > sizeof(q->str))
		{
			q->len = sizeof(q->str);
		}
		memcpy(q->str, s, q->len);
		q->str[sizeof(q->str) - 1] = '\0';
		break;
	case EVQ_BITS:
		q->ret = query_bits(dev, q->which, &bits, &max);
		if (q->ret == 0)
		{
			query_merge(q, bits, max);
		}
		break;
	case EVQ_STATE:
		q->ret = query_state(dev, q->which, &bits, &max);
		if (q->ret == 0)
		{
			query_merge(q, bits, max);
		}
		break;
	case EVQ_PROP:
		query_merge(q, dev->propbit, INPUT_PROP_MAX);
		break;
	case EVQ_ABS:
		if (!dev->absinfo)
		{
			q->ret = -EINVAL;
			break;
		}
		q->abs = dev->absinfo[q->which];
		break;
	case EVQ_SET_ABS:
		if (!dev->absinfo || q->which == ABS_MT_SLOT)
		{
			q->ret = -EINVAL;
			break;
		}
		dev->absinfo[q->which] = q->abs;
		break;
	case EVQ_REP:
		q->ret = test_bit(EV_REP, dev->evbit) ? -EINVAL : -ENOSYS;
		break;
	}
}

/*
 * Run @q on the client's device; on the legacy mux, OR it over every
 * device (bitmaps) and answer identity queries for the mux itself.
 */
static int evdev_query_run(struct evdev_client *c, struct evdev_query *q)
{
	struct evdev *list[EVDEV_MINORS];
	unsigned long flags;
	int n = 0;
	int i;

	q->ret = 0;
	q->len = 0;
	memset(q->bits, 0, sizeof(q->bits));
	if (!c->evdev->is_mux)
	{
		if (input_handle_locked(&c->evdev->handle, query_fill, q) != 0)
		{
			return -ENODEV;
		}
		return q->ret;
	}

	switch (q->kind)
	{
	case EVQ_ID:
		memset(&q->id, 0, sizeof(q->id));
		q->id.bustype = BUS_VIRTUAL;
		return 0;
	case EVQ_STR:
		if (q->which != 0)
		{
			return -ENOENT;
		}
		q->len = strlen("IR0 events0") + 1u;
		memcpy(q->str, "IR0 events0", q->len);
		return 0;
	case EVQ_ABS:
	case EVQ_SET_ABS:
		return -EINVAL;
	case EVQ_REP:
		return -ENOSYS;
	default:
		break;
	}

	flags = ir0_spin_lock_irqsave(&evdev_lock);
	for (i = 0; i < EVDEV_MINORS; i++)
	{
		if (evdev_table[i] && evdev_table[i]->exist)
		{
			evdev_table[i]->refs++;
			list[n++] = evdev_table[i];
		}
	}
	ir0_spin_unlock_irqrestore(&evdev_lock, flags);
	for (i = 0; i < n; i++)
	{
		(void)input_handle_locked(&list[i]->handle, query_fill, q);
		evdev_put(list[i]);
	}
	if (n == 0)
	{
		/* No device: an empty map of the same length. */
		static struct input_dev none;

		query_fill(&none, q);
	}
	return q->ret;
}

static struct evdev *mux_target(unsigned int type)
{
	struct evdev_query q;
	unsigned long flags;
	struct evdev *found = NULL;
	int i;

	for (i = 0; i < EVDEV_MINORS && !found; i++)
	{
		struct evdev *e;

		flags = ir0_spin_lock_irqsave(&evdev_lock);
		e = evdev_table[i];
		if (e && e->exist)
		{
			e->refs++;
		}
		else
		{
			e = NULL;
		}
		ir0_spin_unlock_irqrestore(&evdev_lock, flags);
		if (!e)
		{
			continue;
		}
		memset(&q, 0, sizeof(q));
		q.kind = EVQ_BITS;
		q.which = 0;
		if (input_handle_locked(&e->handle, query_fill, &q) == 0 &&
		    type <= EV_MAX && test_bit(type, q.bits))
		{
			found = e;
			continue;
		}
		evdev_put(e);
	}
	return found;
}

/* Copy min(@len, @size) bytes; bits_to_user()/str_to_user() return that. */
static int64_t evdev_copy_out(void *arg, const void *src, size_t len,
			      size_t size)
{
	size_t n = len < size ? len : size;

	if (copy_to_user(arg, src, n) != 0)
	{
		return -EFAULT;
	}
	return (int64_t)n;
}

static int evdev_set_clk_type(struct evdev_client *c, int clkid)
{
	unsigned long flags;
	int clk_type;

	switch (clkid)
	{
	case CLOCK_REALTIME:
		clk_type = EV_CLK_REAL;
		break;
	case CLOCK_MONOTONIC:
		clk_type = EV_CLK_MONO;
		break;
	case CLOCK_BOOTTIME:
		clk_type = EV_CLK_BOOT;
		break;
	default:
		return -EINVAL;
	}
	flags = ir0_spin_lock_irqsave(&evdev_lock);
	if (c->clk_type != clk_type)
	{
		c->clk_type = clk_type;
		/* Queued events carry the old clock: flush them. */
		if (c->head != c->tail)
		{
			c->packet_head = c->head = c->tail;
			client_queue_syn_dropped(c);
		}
	}
	ir0_spin_unlock_irqrestore(&evdev_lock, flags);
	return 0;
}

static int evdev_revoke(struct evdev_client *c)
{
	unsigned long flags;

	flags = ir0_spin_lock_irqsave(&evdev_lock);
	c->revoked = 1;
	ir0_spin_unlock_irqrestore(&evdev_lock, flags);
	(void)evdev_ungrab(c);
	input_event_wake_readers();
	return 0;
}

static void evdev_caps(struct evdev_client *c, struct ir0_input_caps *caps)
{
	struct evdev_query q;
	unsigned long flags;
	int i;

	memset(caps, 0, sizeof(*caps));
	caps->has_events0 = 1;
	for (i = 0; i < EVDEV_MINORS; i++)
	{
		struct evdev *e;
		int key;
		int rel;
		int i8042;

		flags = ir0_spin_lock_irqsave(&evdev_lock);
		e = evdev_table[i];
		if (e && e->exist)
		{
			e->refs++;
		}
		else
		{
			e = NULL;
		}
		ir0_spin_unlock_irqrestore(&evdev_lock, flags);
		if (!e)
		{
			continue;
		}
		memset(&q, 0, sizeof(q));
		q.kind = EVQ_BITS;
		q.which = EV_KEY;
		(void)input_handle_locked(&e->handle, query_fill, &q);
		key = test_bit(KEY_A, q.bits);
		memset(&q, 0, sizeof(q));
		q.kind = EVQ_BITS;
		q.which = EV_REL;
		(void)input_handle_locked(&e->handle, query_fill, &q);
		rel = test_bit(REL_X, q.bits);
		memset(&q, 0, sizeof(q));
		q.kind = EVQ_ID;
		(void)input_handle_locked(&e->handle, query_fill, &q);
		i8042 = q.id.bustype == BUS_I8042;
		evdev_put(e);
		caps->keyboard |= key;
		caps->keyboard_ps2 |= key && i8042;
		caps->mouse |= rel;
		caps->mouse_ps2 |= rel && i8042;
	}
	caps->supports_key_up_down = caps->keyboard;
	caps->supports_mouse_motion = caps->mouse;
	flags = ir0_spin_lock_irqsave(&evdev_lock);
	caps->event_queue_depth = (int)((c->head - c->tail) &
					(EVDEV_BUFFER_SIZE - 1u));
	ir0_spin_unlock_irqrestore(&evdev_lock, flags);
}

static int64_t evdev_legacy_ioctl(struct evdev_client *c, uint64_t request,
				  void *arg)
{
	if (request == IR0_INPUT_IOCTL_GET_CAPS)
	{
		struct ir0_input_caps caps;

		if (!arg)
		{
			return -EFAULT;
		}
		evdev_caps(c, &caps);
		return copy_to_user(arg, &caps, sizeof(caps)) ? -EFAULT : 0;
	}
#if CONFIG_TEST_INPUT_INJECT
	{
		struct ir0_input_event ev;
		struct evdev *target;

		if (!arg || copy_from_user(&ev, arg, sizeof(ev)) != 0)
		{
			return -EFAULT;
		}
		target = c->evdev->is_mux ? mux_target(ev.type) : c->evdev;
		if (!target)
		{
			return -ENODEV;
		}
		input_inject_event(&target->handle, ev.type, ev.code, ev.value);
		/* Linux clients expect a SYN_REPORT after injected edges. */
		if (ev.type != EV_SYN)
		{
			input_inject_event(&target->handle, EV_SYN, SYN_REPORT, 0);
		}
		if (c->evdev->is_mux)
		{
			evdev_put(target);
		}
		return 0;
	}
#else
	(void)mux_target;
	return -EINVAL;
#endif
}

#define EVIOC_MASK_SIZE(nr) ((nr) & ~(_IOC_SIZEMASK << _IOC_SIZESHIFT))

/* evdev_do_ioctl(): anything it does not know is -EINVAL. */
static int64_t evdev_ioctl(void *priv, uint64_t request, void *arg)
{
	struct evdev_client *c = priv;
	struct evdev_query q;
	unsigned int cmd = (unsigned int)request;
	unsigned int size = _IOC_SIZE(cmd);
	unsigned int nr = _IOC_NR(cmd);
	int value;
	int64_t ret;

	if (!c)
	{
		return -EBADF;
	}
	if (!c->evdev->exist || c->revoked)
	{
		return -ENODEV;
	}
	if (request == IR0_INPUT_IOCTL_GET_CAPS ||
	    request == IR0_INPUT_IOCTL_INJECT)
	{
		return evdev_legacy_ioctl(c, request, arg);
	}

	memset(&q, 0, sizeof(q));
	switch (cmd)
	{
	case EVIOCGVERSION:
		value = EV_VERSION;
		return copy_to_user(arg, &value, sizeof(value)) ? -EFAULT : 0;
	case EVIOCGID:
		q.kind = EVQ_ID;
		ret = evdev_query_run(c, &q);
		if (ret)
		{
			return ret;
		}
		return copy_to_user(arg, &q.id, sizeof(q.id)) ? -EFAULT : 0;
	case EVIOCGREP:
	case EVIOCSREP:
		q.kind = EVQ_REP;
		return evdev_query_run(c, &q);
	case EVIOCGKEYCODE:
	case EVIOCGKEYCODE_V2:
	case EVIOCSKEYCODE:
	case EVIOCSKEYCODE_V2:
		/* No device here has a keycode table. */
		return -EINVAL;
	case EVIOCGEFFECTS:
		value = 0;
		return copy_to_user(arg, &value, sizeof(value)) ? -EFAULT : 0;
	case EVIOCRMFF:
		return -ENOSYS;
	case EVIOCGRAB:
		if (c->evdev->is_mux)
		{
			return -EINVAL;
		}
		return arg ? evdev_grab(c) : evdev_ungrab(c);
	case EVIOCREVOKE:
		if (arg)
		{
			return -EINVAL;
		}
		return evdev_revoke(c);
	case EVIOCSCLOCKID:
		if (copy_from_user(&value, arg, sizeof(value)) != 0)
		{
			return -EFAULT;
		}
		return evdev_set_clk_type(c, value);
	default:
		break;
	}

	switch (EVIOC_MASK_SIZE(cmd))
	{
	case EVIOCGPROP(0):
		q.kind = EVQ_PROP;
		ret = evdev_query_run(c, &q);
		return ret ? ret : evdev_copy_out(arg, q.bits, q.len, size);
	case EVIOCGMTSLOTS(0):
		return -EINVAL;
	case EVIOCGKEY(0):
	case EVIOCGLED(0):
	case EVIOCGSND(0):
	case EVIOCGSW(0):
	{
		unsigned int type = EVIOC_MASK_SIZE(cmd) == EVIOCGKEY(0) ? EV_KEY :
				    EVIOC_MASK_SIZE(cmd) == EVIOCGLED(0) ? EV_LED :
				    EVIOC_MASK_SIZE(cmd) == EVIOCGSND(0) ? EV_SND :
				    EV_SW;
		unsigned long flags;

		q.kind = EVQ_STATE;
		q.which = type;
		ret = evdev_query_run(c, &q);
		if (ret)
		{
			return ret;
		}
		/* The state replaces the queued events of that type. */
		flags = ir0_spin_lock_irqsave(&evdev_lock);
		client_flush_type(c, type);
		ir0_spin_unlock_irqrestore(&evdev_lock, flags);
		ret = evdev_copy_out(arg, q.bits, q.len, size);
		if (ret < 0)
		{
			flags = ir0_spin_lock_irqsave(&evdev_lock);
			client_queue_syn_dropped(c);
			ir0_spin_unlock_irqrestore(&evdev_lock, flags);
		}
		return ret;
	}
	case EVIOCGNAME(0):
	case EVIOCGPHYS(0):
	case EVIOCGUNIQ(0):
		q.kind = EVQ_STR;
		q.which = EVIOC_MASK_SIZE(cmd) == EVIOCGNAME(0) ? 0 :
			  EVIOC_MASK_SIZE(cmd) == EVIOCGPHYS(0) ? 1 : 2;
		ret = evdev_query_run(c, &q);
		return ret ? ret : evdev_copy_out(arg, q.str, q.len, size);
	case EVIOC_MASK_SIZE(EVIOCSFF):
		return -ENOSYS;
	default:
		break;
	}

	if (_IOC_TYPE(cmd) != 'E')
	{
		return -EINVAL;
	}
	if (_IOC_DIR(cmd) == _IOC_READ)
	{
		if ((nr & ~EV_MAX) == _IOC_NR(EVIOCGBIT(0, 0)))
		{
			q.kind = EVQ_BITS;
			q.which = nr & EV_MAX;
			ret = evdev_query_run(c, &q);
			return ret ? ret : evdev_copy_out(arg, q.bits, q.len, size);
		}
		if ((nr & ~ABS_MAX) == _IOC_NR(EVIOCGABS(0)))
		{
			q.kind = EVQ_ABS;
			q.which = nr & ABS_MAX;
			ret = evdev_query_run(c, &q);
			if (ret)
			{
				return ret;
			}
			ret = evdev_copy_out(arg, &q.abs, sizeof(q.abs), size);
			return ret < 0 ? ret : 0;
		}
	}
	if (_IOC_DIR(cmd) == _IOC_WRITE &&
	    (nr & ~ABS_MAX) == _IOC_NR(EVIOCSABS(0)))
	{
		size_t n = size < sizeof(q.abs) ? size : sizeof(q.abs);

		if (copy_from_user(&q.abs, arg, n) != 0)
		{
			return -EFAULT;
		}
		if (size < sizeof(struct input_absinfo))
		{
			q.abs.resolution = 0;
		}
		q.kind = EVQ_SET_ABS;
		q.which = nr & ABS_MAX;
		return evdev_query_run(c, &q);
	}
	return -EINVAL;
}

static const struct devfs_file_ops evdev_fops = {
	.open = evdev_open,
	.get = evdev_get,
	.release = evdev_release,
	.read = evdev_read,
	.write = evdev_write,
	.ioctl = evdev_ioctl,
	.poll = evdev_poll,
};

int input_events_readers_active(void)
{
	return events0_readers > 0;
}

int evdev_init(void)
{
	evdev_mux.is_mux = 1;
	evdev_mux.exist = 1;
	evdev_mux.refs = 1;
	evdev_mux.node.entry.name = "events0";
	evdev_mux.node.entry.mode = 0660;
	evdev_mux.node.entry.device_id = EVDEV_EVENTS0_ID;
	evdev_mux.node.entry.driver_data = &evdev_mux;
	evdev_mux.node.fops = &evdev_fops;
	if (devfs_register_node(&evdev_mux.node) != 0)
	{
		return -ENOMEM;
	}
	return input_register_handler(&evdev_handler);
}
