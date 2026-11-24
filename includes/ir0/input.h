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
 * Description: IR0 kernel source/header file
 */

/**
 * IR0 Kernel - Linux evdev-compatible input events
 * 
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <linux/input.h>

/*
 * /dev/events0 test hook ioctl (gated by CONFIG_TEST_INPUT_INJECT).
 * Only used by deterministic smoke/harness code.
 */
#define IR0_INPUT_IOCTL_INJECT 0x49520001u
#define IR0_INPUT_IOCTL_GET_CAPS 0x49520002u

struct input_dev;

/* One registered device. Events are reported only through input_event. */
int input_register_device(struct input_dev *dev);
struct input_dev *input_i8042_dev(void);
void input_event(struct input_dev *dev, unsigned int type,
		 unsigned int code, int value);
void input_sync(struct input_dev *dev);
/* EVIOCGKEY: current key bitmap. Fills min(len, bitmap) from dev. */
size_t input_key_state(struct input_dev *dev, void *buf, size_t len);

const char *input_device_name(void);
int input_device_id(struct input_id *id);

/* One ring per open of the event node. */
int input_is_event_device(uint32_t device_id);
void *input_client_open(void);
void input_client_get(void *client);
void input_client_put(void *client);
/*
 * EVIOCGRAB: the third ioctl argument is the value itself (1 grabs,
 * 0 releases), not a user pointer.
 */
/* 0 on success. A second grabber gets -EBUSY. */
int input_client_grab(void *client, int grab);
/*
 * EVIOCSCLOCKID: the third argument is a pointer to the clock id
 * (CLOCK_REALTIME 0 or CLOCK_MONOTONIC 1).
 */
/* 0 on success. Any other clock id is -EINVAL. */
int input_client_set_clock(void *client, int clockid);
void input_set_read_context(void *client, int nonblock);
int input_read_is_nonblock(void);

/*
 * /dev/events0 open-reader count. While >0, PS/2 still feeds EV_KEY but
 * must not also inject cooked TTY bytes (each key would reach the evdev reader and the TTY reader).
 */
void input_events_reader_open(void);
void input_events_reader_close(void);
int input_events_readers_active(void);

/* Read events (devfs /dev/events0) */
size_t input_event_read(struct input_event *buf, size_t count);
int input_event_has_data(void);
size_t input_event_queue_depth(void);

/*
 * IR0 input facade (device/provider neutral).
 * Current provider: keyboard -> /dev/events0 queue.
 */
struct ir0_input_event {
    uint16_t type;
    uint16_t code;
    int32_t value;
    int64_t timestamp_ms;
};

struct ir0_input_caps {
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

int ir0_input_poll(void);
int ir0_input_read_event(struct ir0_input_event *ev);
void ir0_input_get_caps(struct ir0_input_caps *caps);
int ir0_input_inject_event(uint16_t type, uint16_t code, int32_t value);

/* IRQ-safe notification after a complete device event/report is queued. */
typedef void (*input_ready_notifier_t)(void);
void input_mouse_set_ready_notifier(input_ready_notifier_t notifier);
void input_event_wake_readers(void);
