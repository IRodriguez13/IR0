/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: input.c
 * Description: Input core, Linux drivers/input/input.c model: the device
 *              and handler lists, the per-device key/LED/switch state, event
 *              filtering against the capability bits, and delivery to the
 *              attached handles (only the grabbing one while grabbed).
 */

#include <ir0/input.h>
#include <ir0/errno.h>
#include <ir0/kmem.h>
#include <ir0/spinlock.h>
#include <string.h>

/*
 * Guards the device and handler lists, every device's handle list, grab,
 * state bitmaps and pending count, and event delivery. Handlers run under
 * it with interrupts masked.
 */
static ir0_spinlock_t input_lock;
static struct input_dev *input_dev_list;
static struct input_handler *input_handler_list;

struct input_dev *input_allocate_device(void)
{
	return kmalloc(sizeof(struct input_dev));
}

void input_free_device(struct input_dev *dev)
{
	if (!dev || dev->registered)
	{
		return;
	}
	kfree(dev->absinfo);
	kfree(dev);
}

static int is_event_supported(unsigned int code, const unsigned long *bits,
			      unsigned int max)
{
	return code <= max && test_bit(code, bits);
}

/* Flip @code in @state when @value differs from it; 1 if it changed. */
static int change_state(unsigned long *state, unsigned int code, int value)
{
	if ((test_bit(code, state) != 0) == (value != 0))
	{
		return 0;
	}
	if (value)
	{
		__set_bit(code, state);
	}
	else
	{
		__clear_bit(code, state);
	}
	return 1;
}

/*
 * input_get_disposition(): pass the event to the handlers only when the
 * device declares it and it changes something. Autorepeat (value 2) of a
 * key bypasses the state.
 */
static int event_passes(struct input_dev *dev, unsigned int type,
			unsigned int code, int value)
{
	switch (type)
	{
	case EV_SYN:
		return 1;
	case EV_KEY:
		if (!is_event_supported(code, dev->keybit, KEY_MAX))
		{
			return 0;
		}
		if (value == 2)
		{
			return 1;
		}
		return change_state(dev->key, code, value);
	case EV_REL:
		return is_event_supported(code, dev->relbit, REL_MAX) &&
		       value != 0;
	case EV_ABS:
		if (!dev->absinfo || !is_event_supported(code, dev->absbit,
							 ABS_MAX))
		{
			return 0;
		}
		if (dev->absinfo[code].value == value)
		{
			return 0;
		}
		dev->absinfo[code].value = value;
		return 1;
	case EV_MSC:
		return is_event_supported(code, dev->mscbit, MSC_MAX);
	case EV_LED:
		return is_event_supported(code, dev->ledbit, LED_MAX) &&
		       change_state(dev->led, code, value);
	case EV_SND:
		return is_event_supported(code, dev->sndbit, SND_MAX) &&
		       change_state(dev->snd, code, value);
	case EV_SW:
		return is_event_supported(code, dev->swbit, SW_MAX) &&
		       change_state(dev->sw, code, value);
	default:
		return 0;
	}
}

/* Caller holds input_lock. */
static void handle_event(struct input_dev *dev, unsigned int type,
			 unsigned int code, int value)
{
	struct input_handle *h;

	if (!event_passes(dev, type, code, value))
	{
		return;
	}
	if (type == EV_SYN && code == SYN_REPORT)
	{
		/* A report with nothing before it is not passed on. */
		if (dev->pending == 0)
		{
			return;
		}
		dev->pending = 0;
	}
	else if (type != EV_SYN)
	{
		dev->pending++;
	}

	if (dev->grab)
	{
		dev->grab->handler->event(dev->grab, type, code, value);
		return;
	}
	for (h = dev->h_list; h; h = h->d_next)
	{
		h->handler->event(h, type, code, value);
	}
}

void input_event(struct input_dev *dev, unsigned int type,
		 unsigned int code, int value)
{
	unsigned long flags;

	if (!dev || type > EV_MAX || !test_bit(type, dev->evbit))
	{
		return;
	}
	flags = ir0_spin_lock_irqsave(&input_lock);
	if (dev->registered)
	{
		handle_event(dev, type, code, value);
	}
	ir0_spin_unlock_irqrestore(&input_lock, flags);
}

void input_inject_event(struct input_handle *handle, unsigned int type,
			unsigned int code, int value)
{
	struct input_dev *dev;
	unsigned long flags;

	if (!handle || type > EV_MAX)
	{
		return;
	}
	flags = ir0_spin_lock_irqsave(&input_lock);
	dev = handle->dev;
	if (dev && dev->registered && test_bit(type, dev->evbit) &&
	    (!dev->grab || dev->grab == handle))
	{
		handle_event(dev, type, code, value);
	}
	ir0_spin_unlock_irqrestore(&input_lock, flags);
}

int input_handle_locked(struct input_handle *handle,
			void (*fn)(struct input_dev *dev, void *arg), void *arg)
{
	unsigned long flags;
	int ret = -ENODEV;

	if (!handle || !fn)
	{
		return -EINVAL;
	}
	flags = ir0_spin_lock_irqsave(&input_lock);
	if (handle->dev && handle->dev->registered)
	{
		fn(handle->dev, arg);
		ret = 0;
	}
	ir0_spin_unlock_irqrestore(&input_lock, flags);
	return ret;
}

int input_register_handle(struct input_handle *handle)
{
	unsigned long flags;

	if (!handle || !handle->dev || !handle->handler ||
	    !handle->handler->event)
	{
		return -EINVAL;
	}
	flags = ir0_spin_lock_irqsave(&input_lock);
	handle->d_next = handle->dev->h_list;
	handle->dev->h_list = handle;
	ir0_spin_unlock_irqrestore(&input_lock, flags);
	return 0;
}

void input_unregister_handle(struct input_handle *handle)
{
	struct input_handle **pp;
	unsigned long flags;

	if (!handle)
	{
		return;
	}
	flags = ir0_spin_lock_irqsave(&input_lock);
	if (handle->dev)
	{
		if (handle->dev->grab == handle)
		{
			handle->dev->grab = NULL;
		}
		for (pp = &handle->dev->h_list; *pp; pp = &(*pp)->d_next)
		{
			if (*pp == handle)
			{
				*pp = handle->d_next;
				break;
			}
		}
		/* Later calls through the handle see no device. */
		handle->dev = NULL;
	}
	ir0_spin_unlock_irqrestore(&input_lock, flags);
}

int input_grab_device(struct input_handle *handle)
{
	unsigned long flags;
	int ret = 0;

	if (!handle)
	{
		return -EINVAL;
	}
	flags = ir0_spin_lock_irqsave(&input_lock);
	if (!handle->dev)
	{
		ret = -ENODEV;
	}
	else if (handle->dev->grab)
	{
		ret = -EBUSY;
	}
	else
	{
		handle->dev->grab = handle;
	}
	ir0_spin_unlock_irqrestore(&input_lock, flags);
	return ret;
}

void input_release_device(struct input_handle *handle)
{
	unsigned long flags;

	if (!handle)
	{
		return;
	}
	flags = ir0_spin_lock_irqsave(&input_lock);
	if (handle->dev && handle->dev->grab == handle)
	{
		handle->dev->grab = NULL;
	}
	ir0_spin_unlock_irqrestore(&input_lock, flags);
}

/*
 * Registration runs in process context (boot or a uinput ioctl), never
 * from an interrupt; connect() may allocate, so it runs unlocked.
 */
int input_register_device(struct input_dev *dev)
{
	struct input_handler *handler;
	unsigned long flags;

	if (!dev || !dev->name || dev->registered)
	{
		return -EINVAL;
	}
	/* Every device can report a sync; none reports KEY_RESERVED. */
	__set_bit(EV_SYN, dev->evbit);
	__clear_bit(KEY_RESERVED, dev->keybit);

	flags = ir0_spin_lock_irqsave(&input_lock);
	dev->registered = 1;
	dev->next = input_dev_list;
	input_dev_list = dev;
	ir0_spin_unlock_irqrestore(&input_lock, flags);

	for (handler = input_handler_list; handler; handler = handler->next)
	{
		(void)handler->connect(handler, dev);
	}
	return 0;
}

void input_unregister_device(struct input_dev *dev)
{
	struct input_dev **pp;
	unsigned long flags;

	if (!dev || !dev->registered)
	{
		return;
	}
	flags = ir0_spin_lock_irqsave(&input_lock);
	dev->registered = 0;
	dev->grab = NULL;
	for (pp = &input_dev_list; *pp; pp = &(*pp)->next)
	{
		if (*pp == dev)
		{
			*pp = dev->next;
			break;
		}
	}
	ir0_spin_unlock_irqrestore(&input_lock, flags);

	/* disconnect() unregisters its handle, which pops the list head. */
	while (dev->h_list)
	{
		struct input_handle *h = dev->h_list;

		h->handler->disconnect(h);
		if (dev->h_list == h)
		{
			input_unregister_handle(h);
		}
	}
	kfree(dev->absinfo);
	kfree(dev);
}

int input_register_handler(struct input_handler *handler)
{
	struct input_dev *dev;
	unsigned long flags;

	if (!handler || !handler->event || !handler->connect ||
	    !handler->disconnect)
	{
		return -EINVAL;
	}
	flags = ir0_spin_lock_irqsave(&input_lock);
	handler->next = input_handler_list;
	input_handler_list = handler;
	ir0_spin_unlock_irqrestore(&input_lock, flags);

	for (dev = input_dev_list; dev; dev = dev->next)
	{
		(void)handler->connect(handler, dev);
	}
	return 0;
}
