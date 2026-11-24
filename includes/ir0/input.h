/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: input.h
 * Description: Input device class, Linux include/linux/input.h model.
 *
 * A driver allocates a struct input_dev, sets its identity and capability
 * bits (__set_bit into evbit/keybit/relbit/...), registers it and reports
 * events with input_event() or the input_report_*() helpers, ending each
 * packet with input_sync(). Handlers (evdev, the console keyboard, the
 * PS/2 mouse multiplexer) attach a struct input_handle to every device
 * they want and receive its events. Drivers never call a handler and
 * handlers never name a driver.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <linux/input.h>
#include <ir0/bitops.h>

struct input_handle;

struct input_dev
{
	const char *name;
	const char *phys;
	const char *uniq;
	struct input_id id;

	unsigned long propbit[BITS_TO_LONGS(INPUT_PROP_CNT)];
	unsigned long evbit[BITS_TO_LONGS(EV_CNT)];
	unsigned long keybit[BITS_TO_LONGS(KEY_CNT)];
	unsigned long relbit[BITS_TO_LONGS(REL_CNT)];
	unsigned long absbit[BITS_TO_LONGS(ABS_CNT)];
	unsigned long mscbit[BITS_TO_LONGS(MSC_CNT)];
	unsigned long ledbit[BITS_TO_LONGS(LED_CNT)];
	unsigned long sndbit[BITS_TO_LONGS(SND_CNT)];
	unsigned long swbit[BITS_TO_LONGS(SW_CNT)];

	/* ABS_CNT entries when the device has absolute axes, else NULL. */
	struct input_absinfo *absinfo;

	/* Current state, kept by the input core. */
	unsigned long key[BITS_TO_LONGS(KEY_CNT)];
	unsigned long led[BITS_TO_LONGS(LED_CNT)];
	unsigned long snd[BITS_TO_LONGS(SND_CNT)];
	unsigned long sw[BITS_TO_LONGS(SW_CNT)];

	/* Driver data. */
	void *private;

	/* Owned by the input core. */
	struct input_dev *next;
	struct input_handle *h_list;
	struct input_handle *grab;
	unsigned int pending;
	int registered;
};

struct input_handler;

/* One handler attached to one device. */
struct input_handle
{
	void *private;
	const char *name;
	struct input_dev *dev;
	struct input_handler *handler;
	struct input_handle *d_next;
};

struct input_handler
{
	const char *name;
	/* Called with interrupts masked; must not sleep or allocate. */
	void (*event)(struct input_handle *handle, unsigned int type,
		      unsigned int code, int value);
	/*
	 * Offered every registered device. 0 when it attached a handle
	 * (input_register_handle), -ENODEV when it does not want the device.
	 */
	int (*connect)(struct input_handler *handler, struct input_dev *dev);
	/* The device is going away: unregister and drop the handle. */
	void (*disconnect)(struct input_handle *handle);
	struct input_handler *next;
};

struct input_dev *input_allocate_device(void);
/* Only for a device that was never registered. */
void input_free_device(struct input_dev *dev);
int input_register_device(struct input_dev *dev);
/* Disconnects every handler and frees the device. */
void input_unregister_device(struct input_dev *dev);

void input_event(struct input_dev *dev, unsigned int type,
		 unsigned int code, int value);
/* An event from a handler (evdev write): honours the device grab. */
void input_inject_event(struct input_handle *handle, unsigned int type,
			unsigned int code, int value);

static inline void input_report_key(struct input_dev *dev, unsigned int code,
				    int value)
{
	input_event(dev, EV_KEY, code, value);
}

static inline void input_report_rel(struct input_dev *dev, unsigned int code,
				    int value)
{
	input_event(dev, EV_REL, code, value);
}

static inline void input_report_abs(struct input_dev *dev, unsigned int code,
				    int value)
{
	input_event(dev, EV_ABS, code, value);
}

static inline void input_sync(struct input_dev *dev)
{
	input_event(dev, EV_SYN, SYN_REPORT, 0);
}

int input_register_handler(struct input_handler *handler);
int input_register_handle(struct input_handle *handle);
void input_unregister_handle(struct input_handle *handle);
/* Route every event of the device to @handle only; -EBUSY if taken. */
int input_grab_device(struct input_handle *handle);
void input_release_device(struct input_handle *handle);

/*
 * Run @fn on the handle's device under the input lock, so a handler can
 * copy device state (names, bitmaps, key state) without racing an event
 * or an unregister. -ENODEV once the device is gone.
 */
int input_handle_locked(struct input_handle *handle,
			void (*fn)(struct input_dev *dev, void *arg), void *arg);

/* Handler registration, from the generic boot table (drivers/init_drv.c). */
int evdev_init(void);
int uinput_init(void);
int mousedev_init(void);

/*
 * Legacy IR0 interfaces of /dev/events0, the multiplexed node that
 * predates /dev/input/eventN. IR0_INPUT_IOCTL_INJECT is gated by
 * CONFIG_TEST_INPUT_INJECT and only used by deterministic smoke code.
 */
#define IR0_INPUT_IOCTL_INJECT 0x49520001u
#define IR0_INPUT_IOCTL_GET_CAPS 0x49520002u

struct ir0_input_event
{
	uint16_t type;
	uint16_t code;
	int32_t value;
	int64_t timestamp_ms;
};

struct ir0_input_caps
{
	int keyboard;
	int mouse;
	int touch;
	int gamepad;
	int has_events0;
	int keyboard_ps2;
	int mouse_ps2;
	int event_queue_depth;
	int supports_key_up_down;
	int supports_mouse_motion;
};

/*
 * Open /dev/events0 readers. While >0 the console keyboard still feeds
 * EV_KEY but does not also type into the tty (legacy events0 contract).
 */
int input_events_readers_active(void);
