/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: kbd_translate.c
 * Description: Keycode to console bytes for the console keyboard handler
 *              (Linux vt keyboard.c kbd_keycode()): modifiers, locks, the
 *              keymap tables and CSI for the cursor keys. No state of its
 *              own; the caller owns the modifiers.
 */

#include <ir0/kbd.h>
#include <ir0/keymap.h>
#include <linux/input-event-codes.h>
#include <linux/kd.h>
#include <linux/keyboard.h>

int kbd_mods_ctrl(const struct keyboard_modifiers *m)
{
	return m && (m->left_ctrl || m->right_ctrl);
}

int kbd_mods_shift(const struct keyboard_modifiers *m)
{
	return m && (m->left_shift || m->right_shift);
}

uint32_t kbd_mods_mask(const struct keyboard_modifiers *m)
{
	uint32_t mask = KBD_MOD_NONE;

	if (!m)
		return KBD_MOD_NONE;
	if (m->left_ctrl)
		mask |= KBD_MOD_LEFT_CTRL;
	if (m->right_ctrl)
		mask |= KBD_MOD_RIGHT_CTRL;
	if (m->left_shift)
		mask |= KBD_MOD_LEFT_SHIFT;
	if (m->right_shift)
		mask |= KBD_MOD_RIGHT_SHIFT;
	if (m->left_alt)
		mask |= KBD_MOD_LEFT_ALT;
	if (m->right_alt)
		mask |= KBD_MOD_RIGHT_ALT;
	if (m->caps_lock)
		mask |= KBD_MOD_CAPS_LOCK;
	if (m->num_lock)
		mask |= KBD_MOD_NUM_LOCK;
	if (m->scroll_lock)
		mask |= KBD_MOD_SCROLL_LOCK;
	return mask;
}

void kbd_mods_release(struct keyboard_modifiers *m)
{
	if (!m)
		return;
	m->left_ctrl = 0;
	m->right_ctrl = 0;
	m->left_shift = 0;
	m->right_shift = 0;
	m->left_alt = 0;
	m->right_alt = 0;
}

static size_t put_str(uint8_t out[8], const char *s)
{
	size_t n = 0;

	while (s[n] && n < 8)
	{
		out[n] = (uint8_t)s[n];
		n++;
	}
	return n;
}

static char ctrl_ascii(char ch)
{
	if (ch >= 'a' && ch <= 'z')
		ch = (char)(ch - 'a' + 'A');
	if ((ch >= '@' && ch <= '_') || ch == ' ')
		return (char)(ch & 0x1f);
	return ch;
}

/* Shift, Ctrl, Alt: state follows press and release; 1 if @code is one. */
static int apply_momentary(struct keyboard_modifiers *m, unsigned int code,
			   int down)
{
	uint8_t *mod;

	switch (code)
	{
	case KEY_LEFTCTRL:
		mod = &m->left_ctrl;
		break;
	case KEY_RIGHTCTRL:
		mod = &m->right_ctrl;
		break;
	case KEY_LEFTSHIFT:
		mod = &m->left_shift;
		break;
	case KEY_RIGHTSHIFT:
		mod = &m->right_shift;
		break;
	case KEY_LEFTALT:
		mod = &m->left_alt;
		break;
	case KEY_RIGHTALT:
		mod = &m->right_alt;
		break;
	default:
		return 0;
	}
	*mod = down ? 1 : 0;
	return 1;
}

/* Locks toggle on the press only (k_lock: neither release nor repeat). */
static int apply_lock(struct keyboard_modifiers *m, unsigned int code,
		      int value)
{
	uint8_t *lock;

	switch (code)
	{
	case KEY_CAPSLOCK:
		lock = &m->caps_lock;
		break;
	case KEY_NUMLOCK:
		lock = &m->num_lock;
		break;
	case KEY_SCROLLLOCK:
		lock = &m->scroll_lock;
		break;
	default:
		return 0;
	}
	if (value == 1)
		*lock = *lock ? 0 : 1;
	return 1;
}

/* The cursor and editing keys: the CSI sequences of the Linux keymap. */
static const char *csi_for(unsigned int code)
{
	switch (code)
	{
	case KEY_UP:
		return "\x1b[A";
	case KEY_DOWN:
		return "\x1b[B";
	case KEY_RIGHT:
		return "\x1b[C";
	case KEY_LEFT:
		return "\x1b[D";
	case KEY_HOME:
		return "\x1b[H";
	case KEY_END:
		return "\x1b[F";
	case KEY_DELETE:
		return "\x1b[3~";
	case KEY_PAGEUP:
		return "\x1b[5~";
	case KEY_PAGEDOWN:
		return "\x1b[6~";
	default:
		return NULL;
	}
}

static size_t translate_ascii(struct keyboard_modifiers *m, unsigned int code,
			      uint8_t out[8])
{
	unsigned short sym;
	char ch;

	switch (code)
	{
	case KEY_ESC:
		out[0] = 0x1b;
		return 1;
	case KEY_BACKSPACE:
		out[0] = '\b';
		return 1;
	case KEY_TAB:
		out[0] = '\t';
		return 1;
	case KEY_ENTER:
		out[0] = '\r';
		return 1;
	case KEY_SPACE:
		/* Ctrl+Space is NUL: not queued. */
		if (kbd_mods_ctrl(m))
			return 0;
		out[0] = ' ';
		return 1;
	default:
		break;
	}
	if (code == KEY_L && kbd_mods_ctrl(m))
	{
		out[0] = '\f';
		return 1;
	}
	/* The keymap covers the keys of the main block (keycode < 89). */
	if (code >= 89)
		return 0;
	sym = keymap_get_entry(kbd_mods_shift(m) ? K_SHIFTTAB : K_NORMTAB, code);
	if (KTYP(sym) != KT_LATIN && KTYP(sym) != KT_LETTER)
		return 0;
	ch = (char)KVAL(sym);
	if (ch == 0)
		return 0;
	/* Caps Lock flips the case of KT_LETTER keysyms. */
	if (m->caps_lock && KTYP(sym) == KT_LETTER)
	{
		if (ch >= 'a' && ch <= 'z')
			ch = (char)(ch - 'a' + 'A');
		else if (ch >= 'A' && ch <= 'Z')
			ch = (char)(ch - 'A' + 'a');
	}
	if (kbd_mods_ctrl(m))
		ch = ctrl_ascii(ch);
	out[0] = (uint8_t)ch;
	return 1;
}

size_t kbd_translate(struct keyboard_modifiers *m, unsigned int keycode,
		     int value, uint8_t out[8])
{
	const char *csi;

	if (!m || !out)
		return 0;
	if (apply_momentary(m, keycode, value != 0))
		return 0;
	if (apply_lock(m, keycode, value))
		return 0;
	if (value == 0)
		return 0;
	csi = csi_for(keycode);
	if (csi)
		return put_str(out, csi);
	return translate_ascii(m, keycode, out);
}
