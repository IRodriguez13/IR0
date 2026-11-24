/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: keyboard.c
 * Description: Console keyboard input handler, Linux drivers/tty/vt/
 *              keyboard.c model: attaches to every device with keyboard
 *              keys, translates EV_KEY through the keymap and the modifier
 *              and lock state, and hands the bytes to the console line
 *              discipline (cooked) or its raw queue (!ICANON).
 */

#include <ir0/kbd.h>
#include <ir0/cmdline.h>
#include <ir0/console.h>
#include <ir0/cpu.h>
#include <ir0/errno.h>
#include <ir0/input.h>
#include <ir0/kmem.h>
#include <ir0/keymap.h>
#include <ir0/ktm/klog.h>
#include <ir0/poll.h>
#include <ir0/spinlock.h>

#define KBD_RING_SIZE 256

/*
 * The raw queue: one producer (the handler, interrupts masked) publishes
 * head, the consumer publishes tail; flushing only moves tail, so neither
 * side resets the other's index (n_tty_data ownership).
 */
static char kbd_ring[KBD_RING_SIZE];
static volatile int kbd_head;
static volatile int kbd_tail;

/* Guards kbd_mods and kbd_mode. Taken inside the input core lock. */
static ir0_spinlock_t kbd_lock;
static struct keyboard_modifiers kbd_mods;
static int kbd_mode = KBD_MODE_XLATE;

static void kbd_queue(char c)
{
	int next = (kbd_head + 1) % KBD_RING_SIZE;
	static int ascii_tag;

	ir0_console_keypress(c);
	if (!ir0_console_store_key_in_ring())
		return;
	if (next == kbd_tail)
	{
		/* Overflow: resync the momentary modifiers. */
		kbd_reset();
		return;
	}
	kbd_ring[kbd_head] = c;
	kbd_head = next;
	(void)poll_wake_check_nosched();
	if (ir0_cmdline_tty_smoke_enabled() && !ascii_tag &&
	    (unsigned char)c >= ' ')
	{
		ascii_tag = 1;
		klog_smoke("KBD_ASCII_OK");
	}
}

/*
 * The bytes go to the line discipline after kbd_lock is dropped: the
 * discipline may signal and resync the keyboard (kbd_reset) on its own.
 */
static void kbd_event(struct input_handle *handle, unsigned int type,
		      unsigned int code, int value)
{
	uint8_t out[8];
	unsigned long flags;
	size_t n = 0;
	size_t i;

	(void)handle;
	if (type != EV_KEY)
		return;
	flags = ir0_spin_lock_irqsave(&kbd_lock);
	if (kbd_mode == KBD_MODE_MEDIUMRAW)
	{
		/* K_MEDIUMRAW: one byte, the keycode with bit 7 on release. */
		if (code < 128)
			out[n++] = (uint8_t)code | (value ? 0 : 0x80);
	}
	else
	{
		n = kbd_translate(&kbd_mods, code, value, out);
	}
	ir0_spin_unlock_irqrestore(&kbd_lock, flags);
	if (n == 0)
		return;
	/*
	 * Legacy /dev/events0 contract: while it has readers translated keys
	 * reach them only, not the console as well.
	 */
	if (kbd_mode != KBD_MODE_MEDIUMRAW && input_events_readers_active())
	{
		static unsigned int suppressed;

		if (++suppressed <= 8)
			klog_notice_fmt("INPUT",
					"[INPUT] ascii suppressed: events0 readers active (n=%u)\n",
					suppressed);
		return;
	}
	for (i = 0; i < n; i++)
		kbd_queue((char)out[i]);
}

/* kbd_match(): a device with any key below BTN_MISC is a keyboard. */
static int kbd_connect(struct input_handler *handler, struct input_dev *dev)
{
	struct input_handle *handle;
	unsigned int i;

	if (!test_bit(EV_KEY, dev->evbit))
		return -ENODEV;
	for (i = KEY_RESERVED; i < BTN_MISC; i++)
	{
		if (test_bit(i, dev->keybit))
			break;
	}
	if (i == BTN_MISC)
		return -ENODEV;
	handle = kmalloc(sizeof(*handle));
	if (!handle)
		return -ENOMEM;
	handle->dev = dev;
	handle->handler = handler;
	handle->name = "kbd";
	if (input_register_handle(handle) != 0)
	{
		kfree(handle);
		return -ENOMEM;
	}
	return 0;
}

static void kbd_disconnect(struct input_handle *handle)
{
	input_unregister_handle(handle);
	kfree(handle);
}

static struct input_handler kbd_handler = {
	.name = "kbd",
	.event = kbd_event,
	.connect = kbd_connect,
	.disconnect = kbd_disconnect,
};

char kbd_getc(void)
{
	char c;

	if (kbd_head == kbd_tail)
		return 0;
	c = kbd_ring[kbd_tail];
	kbd_tail = (kbd_tail + 1) % KBD_RING_SIZE;
	return c;
}

int kbd_has_data(void)
{
	return kbd_head != kbd_tail;
}

void kbd_flush(void)
{
	int guard = KBD_RING_SIZE;

	/* Only tail moves; bounded because a held key refills the ring. */
	while (guard-- > 0 && kbd_head != kbd_tail)
		kbd_tail = (kbd_tail + 1) % KBD_RING_SIZE;
}

void kbd_reset(void)
{
	unsigned long flags;

	flags = ir0_spin_lock_irqsave(&kbd_lock);
	kbd_mods_release(&kbd_mods);
	ir0_spin_unlock_irqrestore(&kbd_lock, flags);
}

void kbd_receive(char c)
{
	unsigned long flags;

	/* Same path as a typed byte, with interrupts masked like it. */
	flags = irq_save();
	kbd_queue(c);
	irq_restore(flags);
}

int kbd_set_mode(int mode)
{
	unsigned long flags;

	if (mode != KBD_MODE_XLATE && mode != KBD_MODE_MEDIUMRAW)
		return -EINVAL;
	flags = ir0_spin_lock_irqsave(&kbd_lock);
	kbd_mode = mode;
	kbd_mods_release(&kbd_mods);
	ir0_spin_unlock_irqrestore(&kbd_lock, flags);
	kbd_flush();
	return 0;
}

int kbd_get_mode(void)
{
	return kbd_mode;
}

int kbd_set_layout(int layout)
{
	return keymap_load_layout(layout);
}

int kbd_get_layout(void)
{
	return keymap_get_layout();
}

int kbd_init(void)
{
	return input_register_handler(&kbd_handler);
}
