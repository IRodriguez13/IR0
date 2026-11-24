/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: uinput.c
 * Description: /dev/uinput, Linux drivers/input/misc/uinput.c model. Each
 *              open describes one input device (UI_SET_*BIT, UI_DEV_SETUP),
 *              registers it (UI_DEV_CREATE) and feeds it with write() of
 *              struct input_event. Closing the descriptor removes it.
 */

#include <ir0/input.h>
#include <ir0/copy_user.h>
#include <ir0/devfs.h>
#include <ir0/errno.h>
#include <ir0/kmem.h>
#include <ir0/spinlock.h>
#include <ir0/types.h>
#include <linux/uinput.h>
#include <string.h>

#define UINPUT_DEVICE_ID	0x120u
#define UINPUT_MISC_MAJOR	10
#define UINPUT_MINOR		223
#define UINPUT_PHYS_MAX		64

enum uinput_state
{
	UIST_NEW_DEVICE,
	UIST_SETUP_COMPLETE,
	UIST_CREATED,
};

struct uinput_device
{
	struct input_dev *dev;
	enum uinput_state state;
	int refs;
	char name[UINPUT_MAX_NAME_SIZE];
	char phys[UINPUT_PHYS_MAX];
};

/* Guards the refcount of every uinput_device. */
static ir0_spinlock_t uinput_lock;

static int uinput_open(devfs_node_t *node, int flags, void **priv)
{
	struct uinput_device *udev;

	(void)node;
	(void)flags;
	udev = kmalloc(sizeof(*udev));
	if (!udev)
	{
		return -ENOMEM;
	}
	udev->refs = 1;
	udev->state = UIST_NEW_DEVICE;
	*priv = udev;
	return 0;
}

static void uinput_get(void *priv)
{
	struct uinput_device *udev = priv;
	unsigned long flags;

	flags = ir0_spin_lock_irqsave(&uinput_lock);
	udev->refs++;
	ir0_spin_unlock_irqrestore(&uinput_lock, flags);
}

static void uinput_destroy_device(struct uinput_device *udev)
{
	if (udev->dev && udev->state == UIST_CREATED)
	{
		/* Frees the device once every handler let go of it. */
		input_unregister_device(udev->dev);
	}
	else if (udev->dev)
	{
		input_free_device(udev->dev);
	}
	udev->dev = NULL;
	udev->state = UIST_NEW_DEVICE;
}

static void uinput_release(void *priv)
{
	struct uinput_device *udev = priv;
	unsigned long flags;
	int last;

	flags = ir0_spin_lock_irqsave(&uinput_lock);
	last = --udev->refs == 0;
	ir0_spin_unlock_irqrestore(&uinput_lock, flags);
	if (!last)
	{
		return;
	}
	uinput_destroy_device(udev);
	kfree(udev);
}

/* No device output (LEDs, force feedback) is forwarded: nothing to read. */
static int64_t uinput_read(void *priv, void *buf, size_t count, int nonblock)
{
	struct uinput_device *udev = priv;

	(void)buf;
	(void)nonblock;
	if (udev->state != UIST_CREATED)
	{
		return -ENODEV;
	}
	if (count != 0 && count < sizeof(struct input_event))
	{
		return -EINVAL;
	}
	return -EAGAIN;
}

static int uinput_poll(void *priv)
{
	(void)priv;
	return 0;
}

/* uinput_inject_events(); the legacy uinput_user_dev setup is not kept. */
static int64_t uinput_write(void *priv, const void *buf, size_t count)
{
	struct uinput_device *udev = priv;
	const struct input_event *in = buf;
	size_t i;

	if (count == 0)
	{
		return 0;
	}
	if (udev->state != UIST_CREATED)
	{
		return -EINVAL;
	}
	if (count < sizeof(struct input_event))
	{
		return -EINVAL;
	}
	for (i = 0; i < count / sizeof(struct input_event); i++)
	{
		input_event(udev->dev, in[i].type, in[i].code, in[i].value);
	}
	return (int64_t)(i * sizeof(struct input_event));
}

static int uinput_create_device(struct uinput_device *udev)
{
	int ret;

	if (udev->state != UIST_SETUP_COMPLETE)
	{
		return -EINVAL;
	}
	if (test_bit(EV_ABS, udev->dev->evbit))
	{
		udev->dev->absinfo = kcalloc(ABS_CNT,
					     sizeof(struct input_absinfo));
		if (!udev->dev->absinfo)
		{
			return -ENOMEM;
		}
	}
	ret = input_register_device(udev->dev);
	if (ret)
	{
		return ret;
	}
	udev->state = UIST_CREATED;
	return 0;
}

static int uinput_dev_setup(struct uinput_device *udev, void *arg)
{
	struct uinput_setup setup;

	if (udev->state == UIST_CREATED)
	{
		return -EINVAL;
	}
	if (copy_from_user(&setup, arg, sizeof(setup)) != 0)
	{
		return -EFAULT;
	}
	if (!setup.name[0])
	{
		return -EINVAL;
	}
	udev->dev->id = setup.id;
	memcpy(udev->name, setup.name, sizeof(udev->name));
	udev->name[sizeof(udev->name) - 1] = '\0';
	udev->dev->name = udev->name;
	udev->state = UIST_SETUP_COMPLETE;
	return 0;
}

/* uinput_set_bit(): only before the device exists, only in range. */
static int uinput_set_bit(struct uinput_device *udev, unsigned long *bits,
			  uint64_t bit, unsigned int max)
{
	if (udev->state == UIST_CREATED || bit > max)
	{
		return -EINVAL;
	}
	__set_bit((unsigned long)bit, bits);
	return 0;
}

static int64_t uinput_ioctl(void *priv, uint64_t request, void *arg)
{
	struct uinput_device *udev = priv;
	struct input_dev *dev;
	uint64_t bit = (uint64_t)(uintptr_t)arg;
	unsigned int version = UINPUT_VERSION;

	if (!udev->dev)
	{
		udev->dev = input_allocate_device();
		if (!udev->dev)
		{
			return -ENOMEM;
		}
	}
	dev = udev->dev;

	switch ((unsigned int)request)
	{
	case UI_GET_VERSION:
		return copy_to_user(arg, &version, sizeof(version)) ? -EFAULT : 0;
	case UI_DEV_CREATE:
		return uinput_create_device(udev);
	case UI_DEV_DESTROY:
		uinput_destroy_device(udev);
		return 0;
	case UI_DEV_SETUP:
		return uinput_dev_setup(udev, arg);
	case UI_SET_EVBIT:
		return uinput_set_bit(udev, dev->evbit, bit, EV_MAX);
	case UI_SET_KEYBIT:
		return uinput_set_bit(udev, dev->keybit, bit, KEY_MAX);
	case UI_SET_RELBIT:
		return uinput_set_bit(udev, dev->relbit, bit, REL_MAX);
	case UI_SET_ABSBIT:
		return uinput_set_bit(udev, dev->absbit, bit, ABS_MAX);
	case UI_SET_MSCBIT:
		return uinput_set_bit(udev, dev->mscbit, bit, MSC_MAX);
	case UI_SET_LEDBIT:
		return uinput_set_bit(udev, dev->ledbit, bit, LED_MAX);
	case UI_SET_SNDBIT:
		return uinput_set_bit(udev, dev->sndbit, bit, SND_MAX);
	case UI_SET_SWBIT:
		return uinput_set_bit(udev, dev->swbit, bit, SW_MAX);
	case UI_SET_PROPBIT:
		return uinput_set_bit(udev, dev->propbit, bit, INPUT_PROP_MAX);
	case UI_SET_PHYS:
	{
		long n;

		if (udev->state == UIST_CREATED)
		{
			return -EINVAL;
		}
		n = strncpy_from_user(udev->phys, arg, sizeof(udev->phys));
		if (n < 0)
		{
			return n;
		}
		udev->phys[sizeof(udev->phys) - 1] = '\0';
		dev->phys = udev->phys;
		return 0;
	}
	default:
		/* Force feedback, UI_ABS_SETUP, UI_GET_SYSNAME: not provided. */
		return -EINVAL;
	}
}

static const struct devfs_file_ops uinput_fops = {
	.open = uinput_open,
	.get = uinput_get,
	.release = uinput_release,
	.read = uinput_read,
	.write = uinput_write,
	.ioctl = uinput_ioctl,
	.poll = uinput_poll,
};

static devfs_node_t uinput_node = {
	.entry = { .name = "uinput", .mode = 0600,
		   .device_id = UINPUT_DEVICE_ID,
		   .rdev = IR0_MKDEV(UINPUT_MISC_MAJOR, UINPUT_MINOR) },
	.fops = &uinput_fops,
};

int uinput_init(void)
{
	return devfs_register_node(&uinput_node);
}
