/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: keymap.c
 * Description: Console keyboard translation tables and their US/LATAM defaults.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/keymap.h>
#include <ir0/errno.h>
#include <ir0/input_backend.h>
#include <ir0/spinlock.h>
#include <config.h>
#include <linux/kd.h>
#include <linux/keyboard.h>

#define KEYMAP_NR_TABLES (K_ALTSHIFTTAB + 1)

/* Linux drivers/tty/vt/keyboard.c max_vals[]: highest KVAL per KTYP. */
static const unsigned char keymap_max_vals[] = {
	[KT_LATIN] = 255,
	[KT_FN] = MAX_NR_FUNC - 1,
	[KT_SPEC] = 19,
	[KT_PAD] = NR_PAD - 1,
	[KT_DEAD] = NR_DEAD - 1,
	[KT_CONS] = 255,
	[KT_CUR] = 3,
	[KT_SHIFT] = NR_SHIFT - 1,
	[KT_META] = 255,
	[KT_ASCII] = NR_ASCII - 1,
	[KT_LOCK] = NR_LOCK - 1,
	[KT_LETTER] = 255,
	[KT_SLOCK] = NR_LOCK - 1,
	[KT_DEAD2] = 255,
	[KT_BRL] = NR_BRL - 1,
	[KT_CSI] = 99,
};

#define KEYMAP_NR_TYPES \
	(sizeof(keymap_max_vals) / sizeof(keymap_max_vals[0]))

static const char latin_us[] = {
	0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0, 0,
	'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0, 0, 'a', 's',
	'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
	'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
};

static const char latin_shift_us[] = {
	0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 0, 0,
	'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 0, 0, 'A', 'S',
	'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|', 'Z', 'X', 'C', 'V',
	'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
};

/* LATAM: printable ASCII subset only (no dead keys). */
static const char latin_latam[] = {
	0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '\'', 0, 0, 0,
	'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', 0, '+', 0, 0, 'a', 's',
	'd', 'f', 'g', 'h', 'j', 'k', 'l', 0, '{', '|', 0, '}', 'z', 'x', 'c', 'v',
	'b', 'n', 'm', ',', '.', '-', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
};

static const char latin_shift_latam[] = {
	0, 0, '!', '"', '#', '$', '%', '&', '/', '(', ')', '=', '?', 0, 0, 0,
	'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', 0, '*', 0, 0, 'A', 'S',
	'D', 'F', 'G', 'H', 'J', 'K', 'L', 0, '[', 0, 0, ']', 'Z', 'X', 'C', 'V',
	'B', 'N', 'M', ';', ':', '_', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
};

/* Shared by the keyboard IRQ (decoder) and process context (ioctls). */
static ir0_spinlock_t keymap_lock;
static unsigned short key_maps[KEYMAP_NR_TABLES][NR_KEYS];
static unsigned char key_map_present[KEYMAP_NR_TABLES];
static int keymap_layout = -1;

static unsigned short keymap_default(int layout, unsigned int table,
				     unsigned int key)
{
	const char *latin;
	unsigned char ch;

	switch (key)
	{
	case 1: return K(KT_LATIN, 0x1b);
	case 14: return K(KT_LATIN, 0x7f);
	case 15: return K(KT_LATIN, '\t');
	case 28: return K_ENTER;
	case 29: return K_CTRL;
	case 42: return K_SHIFT;
	case 54: return K_SHIFT;
	case 56: return K_ALT;
	case 58: return K_CAPS;
	default:
		break;
	}
	if (key >= 59 && key <= 68)
		return K(KT_FN, key - 59);
	if (table > K_SHIFTTAB || key >= sizeof(latin_us))
		return K_HOLE;

	if (layout == IR0_KBD_LAYOUT_LATAM)
		latin = table == K_SHIFTTAB ? latin_shift_latam : latin_latam;
	else
		latin = table == K_SHIFTTAB ? latin_shift_us : latin_us;
	ch = (unsigned char)latin[key];
	if (ch == 0)
		return K_HOLE;
	if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'))
		return K(KT_LETTER, ch);
	return K(KT_LATIN, ch);
}

static void keymap_load_locked(int layout)
{
	unsigned int table;
	unsigned int key;

	for (table = 0; table < KEYMAP_NR_TABLES; table++)
	{
		for (key = 0; key < NR_KEYS; key++)
			key_maps[table][key] = keymap_default(layout, table, key);
		key_map_present[table] = 1;
	}
	keymap_layout = layout;
}

static void keymap_ensure_loaded(void)
{
	if (keymap_layout >= 0)
		return;
	keymap_load_locked(CONFIG_KEYBOARD_LAYOUT == IR0_KBD_LAYOUT_LATAM ?
			   IR0_KBD_LAYOUT_LATAM : IR0_KBD_LAYOUT_US);
}

unsigned short keymap_get_entry(unsigned int table, unsigned int index)
{
	unsigned long flags;
	unsigned short value;

	if (index >= NR_KEYS)
		return K_HOLE;

	flags = ir0_spin_lock_irqsave(&keymap_lock);
	keymap_ensure_loaded();
	if (table >= KEYMAP_NR_TABLES || !key_map_present[table])
	{
		value = index ? K_HOLE : K_NOSUCHMAP;
	}
	else
	{
		value = key_maps[table][index];
		if (KTYP(value) >= KEYMAP_NR_TYPES)
			value = K_HOLE;
	}
	ir0_spin_unlock_irqrestore(&keymap_lock, flags);
	return value;
}

int keymap_set_entry(unsigned int table, unsigned int index,
		     unsigned short value)
{
	unsigned long flags;
	unsigned int key;

	if (index == 0 && value == K_NOSUCHMAP)
	{
		if (table == 0 || table >= KEYMAP_NR_TABLES)
			return 0;
		flags = ir0_spin_lock_irqsave(&keymap_lock);
		keymap_ensure_loaded();
		key_map_present[table] = 0;
		ir0_spin_unlock_irqrestore(&keymap_lock, flags);
		return 0;
	}

	if (KTYP(value) >= KEYMAP_NR_TYPES ||
	    KVAL(value) > keymap_max_vals[KTYP(value)])
		return -EINVAL;
	/* Linux: a store to entry 0 only validates the arguments. */
	if (index == 0)
		return 0;
	if (table >= KEYMAP_NR_TABLES || index >= NR_KEYS)
		return -EINVAL;

	flags = ir0_spin_lock_irqsave(&keymap_lock);
	keymap_ensure_loaded();
	if (!key_map_present[table])
	{
		for (key = 0; key < NR_KEYS; key++)
			key_maps[table][key] = K_HOLE;
		key_map_present[table] = 1;
	}
	key_maps[table][index] = value;
	ir0_spin_unlock_irqrestore(&keymap_lock, flags);
	return 0;
}

int keymap_load_layout(int layout)
{
	unsigned long flags;

	if (layout != IR0_KBD_LAYOUT_US && layout != IR0_KBD_LAYOUT_LATAM)
		return -EINVAL;
	flags = ir0_spin_lock_irqsave(&keymap_lock);
	keymap_load_locked(layout);
	ir0_spin_unlock_irqrestore(&keymap_lock, flags);
	return 0;
}

int keymap_get_layout(void)
{
	unsigned long flags;
	int layout;

	flags = ir0_spin_lock_irqsave(&keymap_lock);
	keymap_ensure_loaded();
	layout = keymap_layout;
	ir0_spin_unlock_irqrestore(&keymap_lock, flags);
	return layout;
}
