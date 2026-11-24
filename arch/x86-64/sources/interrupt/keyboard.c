/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: keyboard.c
 * Description: i8042 port 1 keyboard (Linux atkbd): the IRQ path demuxes
 *              the controller byte, decodes scancode set 1 and reports
 *              keys to its input device. The console keyboard handler turns
 *              them into console input.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <interrupt/idt.h>
#include <interrupt/pic.h>
#include <interrupt/keyboard.h>
#include <ir0/cpu.h>

#define PS2_DATA_PORT 0x60
#include <config.h>
#include <ir0/clock.h>
#include <ir0/errno.h>
#include <ir0/irq.h>
#include <ir0/vga.h>
#include <ir0/input.h>
#include <ir0/ktm/klog.h>
#include <ir0/cmdline.h>
#include <ir0/console.h>
#include <ir0/ps2_set1.h>
#include <kernel/syscalls/syscalls.h>
#include <asm/io.h>

#define PS2_STATUS_PORT        0x64
#define PS2_STATUS_OUTPUT_FULL 0x01
/* i8042: output buffer byte is from AUX (mouse) when set — Linux I8042_STR_AUXDATA */
#define PS2_STATUS_AUXDATA     0x20

static void keyboard_feed_scancode(uint8_t scancode);

volatile char *shared_keyboard_buffer = (volatile char *)KEYBOARD_BUFFER_ADDR;
volatile int *shared_keyboard_buffer_pos =
    (volatile int *)(KEYBOARD_BUFFER_ADDR + KEYBOARD_BUFFER_SIZE);

static int system_in_idle_mode = 0;
/* The port-2 (AUX) device driver, if one bound it. */
static void (*i8042_aux_feed)(uint8_t data);

static struct ps2_set1_state kbd_state;
/* The AT keyboard behind i8042 port 1 (Linux atkbd). */
static struct input_dev *kbd_dev;

void keyboard_poll_ps2(void)
{
	static int kbd_poll_tag;

	for (;;)
	{
		uint8_t status;
		uint8_t data;
		unsigned long flags;
		int claimed = 0;

		/*
		 * Claim the byte atomically. The idle task drains the
		 * controller with interrupts enabled, so IRQ1 can land between
		 * the status test and the data read; the byte is consumed by
		 * the handler and this read returns the i8042's last byte
		 * again, duplicating a keystroke ("uptimee", "/proc//uptime").
		 * Linux serialises the same pair under i8042_lock.
		 */
		flags = irq_save();
		status = inb(PS2_STATUS_PORT);
		if (status & PS2_STATUS_OUTPUT_FULL)
		{
			/*
			 * Classify BEFORE reading would be ideal; i8042 latches
			 * AUXDATA with the byte, so read status then data and
			 * branch on AUXDATA.
			 */
			data = inb(PS2_DATA_PORT);
			claimed = 1;
		}
		else
		{
			data = 0;
		}
		irq_restore(flags);

		if (!claimed)
			break;

#if DEBUG_PS2
		kprintf("[PS2] status=0x%02x data=0x%02x source=%s\n", status, data,
			(status & PS2_STATUS_AUXDATA) ? "mouse" : "keyboard");
#endif

		if (status & PS2_STATUS_AUXDATA)
		{
			/* Without an AUX driver the byte is discarded. */
			if (i8042_aux_feed)
				i8042_aux_feed(data);
			continue;
		}

		if (ir0_cmdline_tty_smoke_enabled() && !kbd_poll_tag)
		{
			kbd_poll_tag = 1;
			klog_smoke("KBD_POLL_OK");
		}
		if (ir0_cmdline_tty_smoke_enabled() && ir0_console_in_userspace())
		{
			static int kbd_user_poll_once;

			if (!kbd_user_poll_once)
			{
				kbd_user_poll_once = 1;
				klog_smoke("KBD_USER_POLL_OK");
			}
		}
		keyboard_feed_scancode(data);
	}
	/* Producer side of the tty/stdin wait: wake readers of new input. */
	stdin_wake_check();
}

static void keyboard_irq(uint32_t irq, void *data)
{
	static int kbd_irq_tag;

	(void)irq;
	(void)data;
	clock_note_irq();
	if (ir0_cmdline_tty_smoke_enabled() && !kbd_irq_tag)
	{
		kbd_irq_tag = 1;
		klog_smoke("KBD_IRQ_OK");
	}

	keyboard_poll_ps2();
}

static void i8042_aux_irq(uint32_t irq, void *data)
{
	(void)irq;
	(void)data;
	clock_note_irq();
	/*
	 * IRQ12 and IRQ1 share the i8042 output buffer: demux by AUXDATA,
	 * never assume this IRQ's byte is the mouse's.
	 */
	keyboard_poll_ps2();
}

/*
 * Lines 1 and 12 are serviced even when the controller is not initialized
 * at boot: firmware leaves the i8042 raising them.
 */
void i8042_irq_init(void)
{
	(void)irq_request(1, keyboard_irq, NULL, "i8042-kbd");
	(void)irq_request(12, i8042_aux_irq, NULL, "i8042-aux");
}

static void keyboard_feed_scancode(uint8_t scancode)
{
	unsigned int kc;
	int down;
	static int kbd_scancode_tag;

	if (!ps2_set1_decode(&kbd_state, scancode, &kc, &down))
		return;
	if (ir0_cmdline_tty_smoke_enabled() && !kbd_scancode_tag && down)
	{
		kbd_scancode_tag = 1;
		klog_smoke("KBD_SCANCODE_OK");
	}
	if (!kc || !kbd_dev)
		return;
	/* atkbd: the scancode, then the key; a held key repeats as 2. */
	input_event(kbd_dev, EV_MSC, MSC_SCAN, scancode & 0x7F);
	input_report_key(kbd_dev, kc,
			 down ? (test_bit(kc, kbd_dev->key) ? 2 : 1) : 0);
	input_sync(kbd_dev);
}

/* Linux atkbd identity for a translated set-2 keyboard. */
static int keyboard_register_input(void)
{
	struct input_dev *dev = input_allocate_device();
	int ret;

	if (!dev)
		return -ENOMEM;
	dev->name = "AT Translated Set 2 keyboard";
	dev->phys = "isa0060/serio0/input0";
	dev->id.bustype = BUS_I8042;
	dev->id.vendor = 0x0001;
	dev->id.product = 0x0001;
	dev->id.version = 0xab41;
	__set_bit(EV_KEY, dev->evbit);
	__set_bit(EV_MSC, dev->evbit);
	__set_bit(MSC_SCAN, dev->mscbit);
	ps2_set1_keybits(dev->keybit);
	ret = input_register_device(dev);
	if (ret)
	{
		input_free_device(dev);
		return ret;
	}
	kbd_dev = dev;
	return 0;
}

void i8042_register_aux(void (*feed)(uint8_t data))
{
	i8042_aux_feed = feed;
}

void keyboard_init(void)
{
	ps2_set1_reset(&kbd_state);
	(void)keyboard_register_input();
}

void set_idle_mode(int is_idle)
{
	system_in_idle_mode = is_idle;
}

int is_in_idle_mode(void)
{
	return system_in_idle_mode;
}

void wakeup_from_idle(void)
{
	if (system_in_idle_mode)
	{
		system_in_idle_mode = 0;
	}
	else
	{
		print_colored("DEBUG: wakeup called but not in idle mode\n",
			      VGA_COLOR_RED, VGA_COLOR_BLACK);
	}
}
