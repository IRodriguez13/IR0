/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: input_events.c
 * Description: IR0 kernel source/header file
 */

/**
 * IR0 Kernel - Per-open evdev rings for /dev/input/event0
 * One producer (device IRQ) fans out to every open client.
 */
#include <stddef.h>
#include <ir0/input.h>
#include <ir0/time.h>
#include <ir0/clock.h>
#include <ir0/console.h>
#include <ir0/input_backend.h>
#include <ir0/errno.h>
#include <ir0/kmem.h>
#include <ir0/spinlock.h>
#include <ir0/poll.h>

/* Guards client rings and the reader count. */
static ir0_spinlock_t input_events_lock;

#define INPUT_CLIENT_RING 64

struct input_dev
{
	const char *name;
	struct input_id id;
	struct input_client *clients;
	int registered;
};

struct input_client
{
	struct input_dev *dev;
	struct input_client *next;
	int refs;
	unsigned int head;
	unsigned int tail;
	struct input_event ring[INPUT_CLIENT_RING];
};

static struct input_dev i8042_dev =
{
	.name = "IR0 i8042",
	.id = { BUS_I8042, 0x0001, 0x0001, 0x0001 },
};

static struct input_client *active_client;
static int read_nonblock;
static volatile int events_readers;

/* Poll core callback: marks matching waiters ready without scheduling in IRQ. */
static input_ready_notifier_t mouse_ready_notifier;

void input_mouse_set_ready_notifier(input_ready_notifier_t notifier)
{
	mouse_ready_notifier = notifier;
}

void input_event_wake_readers(void)
{
	(void)poll_wake_check_nosched();
	if (mouse_ready_notifier)
	{
		mouse_ready_notifier();
	}
}

void input_events_reader_open(void)
{
	unsigned long irq_flags;

	irq_flags = ir0_spin_lock_irqsave(&input_events_lock);
	events_readers++;
	ir0_spin_unlock_irqrestore(&input_events_lock, irq_flags);
}

void input_events_reader_close(void)
{
	int do_flush = 0;
	unsigned long irq_flags;

	irq_flags = ir0_spin_lock_irqsave(&input_events_lock);
	if (events_readers > 0)
	{
		events_readers--;
	}
	do_flush = (events_readers == 0);
	ir0_spin_unlock_irqrestore(&input_events_lock, irq_flags);
	if (do_flush)
	{
		/*
		 * Last /dev/events0 reader: drop cooked LD state and any raw
		 * ASCII that may have landed while divert was ending. Flush
		 * alone no longer clears the kbd ring under ICANON.
		 */
		ir0_console_flush_input();
		input_kbd_clear();
	}
}

int input_events_readers_active(void)
{
	return events_readers > 0;
}

int input_register_device(struct input_dev *dev)
{
	if (!dev || !dev->name)
	{
		return -EINVAL;
	}
	dev->registered = 1;
	return 0;
}

struct input_dev *input_i8042_dev(void)
{
	return &i8042_dev;
}

const char *input_device_name(void)
{
	if (!i8042_dev.registered)
	{
		return NULL;
	}
	return i8042_dev.name;
}

int input_device_id(struct input_id *id)
{
	if (!id || !i8042_dev.registered)
	{
		return -ENODEV;
	}
	*id = i8042_dev.id;
	return 0;
}

static void client_push(struct input_client *c, const struct input_event *ev)
{
	unsigned int next = (c->head + 1u) % INPUT_CLIENT_RING;

	if (next == c->tail)
	{
		return;
	}
	c->ring[c->head] = *ev;
	c->head = next;
}

void input_event(struct input_dev *dev, unsigned int type,
		 unsigned int code, int value)
{
	struct input_event ev;
	struct input_client *c;
	uint64_t ms;
	unsigned long irq_flags;
	int queued = 0;

	if (!dev || !dev->registered)
	{
		return;
	}

	ms = clock_get_uptime_milliseconds();
	ev.time.tv_sec = (time_t)(ms / 1000u);
	ev.time.tv_usec = (suseconds_t)((ms % 1000u) * 1000u);
	ev.type = (uint16_t)type;
	ev.code = (uint16_t)code;
	ev.value = value;

	irq_flags = ir0_spin_lock_irqsave(&input_events_lock);
	for (c = dev->clients; c; c = c->next)
	{
		client_push(c, &ev);
		queued = 1;
	}
	ir0_spin_unlock_irqrestore(&input_events_lock, irq_flags);
	if (queued)
	{
		input_event_wake_readers();
	}
}

void input_sync(struct input_dev *dev)
{
	input_event(dev, EV_SYN, SYN_REPORT, 0);
}

void *input_client_open(void)
{
	struct input_client *c;
	unsigned long irq_flags;

	if (!i8042_dev.registered)
	{
		return NULL;
	}
	c = kmalloc(sizeof(*c));
	if (!c)
	{
		return NULL;
	}
	c->dev = &i8042_dev;
	c->refs = 1;
	c->head = 0;
	c->tail = 0;
	irq_flags = ir0_spin_lock_irqsave(&input_events_lock);
	c->next = i8042_dev.clients;
	i8042_dev.clients = c;
	ir0_spin_unlock_irqrestore(&input_events_lock, irq_flags);
	return c;
}

void input_client_get(void *client)
{
	struct input_client *c = client;
	unsigned long irq_flags;

	if (!c)
	{
		return;
	}
	irq_flags = ir0_spin_lock_irqsave(&input_events_lock);
	c->refs++;
	ir0_spin_unlock_irqrestore(&input_events_lock, irq_flags);
}

void input_client_put(void *client)
{
	struct input_client *c = client;
	struct input_client **pp;
	int free_it = 0;
	unsigned long irq_flags;

	if (!c)
	{
		return;
	}
	irq_flags = ir0_spin_lock_irqsave(&input_events_lock);
	if (c->refs > 0)
	{
		c->refs--;
	}
	if (c->refs == 0)
	{
		for (pp = &c->dev->clients; *pp; pp = &(*pp)->next)
		{
			if (*pp == c)
			{
				*pp = c->next;
				break;
			}
		}
		if (active_client == c)
		{
			active_client = NULL;
		}
		free_it = 1;
	}
	ir0_spin_unlock_irqrestore(&input_events_lock, irq_flags);
	if (free_it)
	{
		kfree(c);
	}
}

void input_set_read_context(void *client, int nonblock)
{
	active_client = client;
	read_nonblock = nonblock ? 1 : 0;
}

int input_read_is_nonblock(void)
{
	return read_nonblock;
}

size_t input_event_read(struct input_event *buf, size_t count)
{
	struct input_client *c;
	size_t n = 0;
	unsigned long irq_flags;

	if (!buf || count == 0)
	{
		return 0;
	}

	irq_flags = ir0_spin_lock_irqsave(&input_events_lock);
	c = active_client;
	if (c)
	{
		while (n < count && c->tail != c->head)
		{
			buf[n] = c->ring[c->tail];
			c->tail = (c->tail + 1u) % INPUT_CLIENT_RING;
			n++;
		}
	}
	ir0_spin_unlock_irqrestore(&input_events_lock, irq_flags);
	return n;
}

int input_event_has_data(void)
{
	struct input_client *c;
	int has;
	unsigned long irq_flags;

	irq_flags = ir0_spin_lock_irqsave(&input_events_lock);
	c = active_client;
	has = (c && c->tail != c->head) ? 1 : 0;
	ir0_spin_unlock_irqrestore(&input_events_lock, irq_flags);
	return has;
}

size_t input_event_queue_depth(void)
{
	struct input_client *c;
	size_t head;
	size_t tail;
	unsigned long irq_flags;

	irq_flags = ir0_spin_lock_irqsave(&input_events_lock);
	c = active_client;
	if (!c)
	{
		ir0_spin_unlock_irqrestore(&input_events_lock, irq_flags);
		return 0;
	}
	head = c->head;
	tail = c->tail;
	ir0_spin_unlock_irqrestore(&input_events_lock, irq_flags);

	if (head >= tail)
	{
		return head - tail;
	}
	return (INPUT_CLIENT_RING - tail) + head;
}
