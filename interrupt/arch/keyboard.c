/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: keyboard.c
 * Description: IR0 kernel source/header file
 */

#include "idt.h"
#include "pic.h"
#include "io.h"
#include "keyboard.h"

/* PS/2 keyboard controller: data port (scancode byte read) */
#define PS2_DATA_PORT 0x60
#include <config.h>
#include <ir0/errno.h>
#include <ir0/vga.h>
#include <ir0/input.h>

/* Forward declarations */
void wakeup_from_idle(void);
void stdin_wake_check(void);
static void keyboard_buffer_add(char c);

/*
 * Kernel-side ring for stdin; separate from the shared userspace layout in
 * config.h (KEYBOARD_BUFFER_*).
 */
#define KERNEL_KBD_RING_SIZE 256
static char keyboard_buffer[KERNEL_KBD_RING_SIZE];
static int keyboard_buffer_head = 0;
static int keyboard_buffer_tail = 0;

volatile char *shared_keyboard_buffer = (volatile char *)KEYBOARD_BUFFER_ADDR;
volatile int *shared_keyboard_buffer_pos =
    (volatile int *)(KEYBOARD_BUFFER_ADDR + KEYBOARD_BUFFER_SIZE);

/* Idle wake coordination */
static int system_in_idle_mode = 0;
static int wake_requested = 0;

static int shift_pressed = 0;
static int ctrl_pressed = 0;
/* Extended scancode prefix (0xE0); next byte may be Page Up/Down etc. */
static int ext_scancode = 0;
static int current_keyboard_layout = KEYBOARD_LAYOUT_US;

/*
 * Escape sequences for shell: ESC + code, where codes are consumed by
 * debug_bins/dbgshell.c (userspace-like shell via SYS_READ).
 * Shell uses only syscalls; scroll reads call SYS_CONSOLE_SCROLL.
 */
#define KEY_ESC_SCROLL_UP     0x01
#define KEY_ESC_SCROLL_DOWN   0x02
#define KEY_ESC_CLEAR_SCREEN  0x03
#define KEY_ESC_HISTORY_UP    0x04
#define KEY_ESC_HISTORY_DOWN  0x05
#define KEY_ESC_CURSOR_LEFT   0x06
#define KEY_ESC_CURSOR_RIGHT  0x07
#define KEY_ESC_CURSOR_HOME   0x08
#define KEY_ESC_CURSOR_END    0x09
#define KEY_ESC_DELETE_CHAR   0x0A

/* Basic scancode -> ASCII (printable subset), US layout */
static const char scancode_to_ascii_us[] = {
    0,    0,    '1',  '2',  '3',  '4',  '5',  '6',  // 0-7
    '7',  '8',  '9',  '0',  '-',  '=',  0,    0,    // 8-15 (backspace, tab - manejados por separado)
    'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',  // 16-23
    'o',  'p',  '[',  ']',  0,    0,    'a',  's',  // 24-31 (enter, ctrl - manejados por separado)
    'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',  // 32-39
    '\'', '`',  0,    '\\', 'z',  'x',  'c',  'v',  // 40-47 (shift)
    'b',  'n',  'm',  ',',  '.',  '/',  0,    '*',  // 48-55 (shift)
    0,    0,    0,    0,    0,    0,    0,    0,    // 64-71 (F5-F12)
    0,    0,    0,    0,    0,    0,    0,    0,    // 72-79 (numpad)
    0,    0,    0,    0,    0,    0,    0,    0,    // 80-87
};

/* PS/2 scancode set 1 -> Linux KEY_* (for /dev/events0, Doom) */
static const uint16_t scancode_to_keycode[256] = {
    [0x01] = KEY_ESC, [0x02] = KEY_1, [0x03] = KEY_2, [0x04] = KEY_3,
    [0x05] = KEY_4, [0x06] = KEY_5, [0x07] = KEY_6, [0x08] = KEY_7,
    [0x09] = KEY_8, [0x0A] = KEY_9, [0x0B] = KEY_0, [0x0C] = KEY_MINUS,
    [0x0D] = KEY_EQUAL, [0x0E] = KEY_BACKSPACE, [0x0F] = KEY_TAB,
    [0x10] = KEY_Q, [0x11] = KEY_W, [0x12] = KEY_E, [0x13] = KEY_R,
    [0x14] = KEY_T, [0x15] = KEY_Y, [0x16] = KEY_U, [0x17] = KEY_I,
    [0x18] = KEY_O, [0x19] = KEY_P, [0x1A] = KEY_LEFTBRACE, [0x1B] = KEY_RIGHTBRACE,
    [0x1C] = KEY_ENTER, [0x1D] = KEY_LEFTCTRL, [0x1E] = KEY_A, [0x1F] = KEY_S,
    [0x20] = KEY_D, [0x21] = KEY_F, [0x22] = KEY_G, [0x23] = KEY_H,
    [0x24] = KEY_J, [0x25] = KEY_K, [0x26] = KEY_L, [0x27] = KEY_SEMICOLON,
    [0x28] = KEY_APOSTROPHE, [0x29] = KEY_GRAVE, [0x2A] = KEY_LEFTSHIFT,
    [0x2B] = KEY_BACKSLASH, [0x2C] = KEY_Z, [0x2D] = KEY_X, [0x2E] = KEY_C,
    [0x2F] = KEY_V, [0x30] = KEY_B, [0x31] = KEY_N, [0x32] = KEY_M,
    [0x33] = KEY_COMMA, [0x34] = KEY_DOT, [0x35] = KEY_SLASH, [0x36] = KEY_RIGHTSHIFT,
    [0x37] = KEY_KPASTERISK, [0x38] = KEY_LEFTALT, [0x39] = KEY_SPACE,
    [0x3A] = KEY_CAPSLOCK, [0x3B] = KEY_F1, [0x3C] = KEY_F2, [0x3D] = KEY_F3,
    [0x3E] = KEY_F4, [0x3F] = KEY_F5, [0x40] = KEY_F6, [0x41] = KEY_F7,
    [0x42] = KEY_F8, [0x43] = KEY_F9, [0x44] = KEY_F10, [0x45] = KEY_NUMLOCK,
    [0x46] = KEY_SCROLLLOCK, [0x47] = KEY_KP7, [0x48] = KEY_KP8, [0x49] = KEY_KP9,
    [0x4A] = KEY_KPMINUS, [0x4B] = KEY_KP4, [0x4C] = KEY_KP5, [0x4D] = KEY_KP6,
    [0x4E] = KEY_KPPLUS, [0x4F] = KEY_KP1, [0x50] = KEY_KP2, [0x51] = KEY_KP3,
    [0x52] = KEY_KP0, [0x53] = KEY_KPDOT, [0x57] = KEY_F11, [0x58] = KEY_F12,
};
/* Extended scancodes (0xE0 prefix) */
static const uint16_t ext_scancode_to_keycode[256] = {
    [0x1C] = KEY_KPENTER, [0x1D] = KEY_RIGHTCTRL, [0x35] = KEY_KPSLASH,
    [0x37] = KEY_SYSRQ, [0x38] = KEY_RIGHTALT, [0x47] = KEY_HOME,
    [0x48] = KEY_UP, [0x49] = KEY_PAGEUP, [0x4B] = KEY_LEFT,
    [0x4D] = KEY_RIGHT, [0x4F] = KEY_END, [0x50] = KEY_DOWN,
    [0x51] = KEY_PAGEDOWN, [0x52] = KEY_INSERT, [0x53] = KEY_DELETE,
    [0x5B] = KEY_LEFTMETA, [0x5C] = KEY_RIGHTMETA,
};

/* Scancode table with Shift held, US layout */
static const char scancode_to_ascii_shift_us[] = {
    0,    0,    '!',  '@',  '#',  '$',  '%',  '^',  // 0-7
    '&',  '*',  '(',  ')',  '_',  '+',  0,    0,    // 8-15
    'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',  // 16-23
    'O',  'P',  '{',  '}',  0,    0,    'A',  'S',  // 24-31
    'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',  // 32-39
    '"',  '~',  0,    '|',  'Z',  'X',  'C',  'V',  // 40-47
    'B',  'N',  'M',  '<',  '>',  '?',  0,    '*',  // 48-55
    0,    0,    0,    0,    0,    0,    0,    0,    // 56-63
    0,    0,    0,    0,    0,    0,    0,    0,    // 64-71
    0,    0,    0,    0,    0,    0,    0,    0,    // 72-79
    0,    0,    0,    0,    0,    0,    0,    0,    // 80-87
};

/* Basic scancode -> ASCII (printable subset), LATAM layout */
static const char scancode_to_ascii_latam[] = {
    0,    0,    '1',  '2',  '3',  '4',  '5',  '6',  // 0-7
    '7',  '8',  '9',  '0',  '\'', 0,    0,    0,    // 8-15
    'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',  // 16-23
    'o',  'p',  '\'', '+',  0,    0,    'a',  's',  // 24-31
    'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',  // 32-39
    '\'', '|',  0,    '}',  'z',  'x',  'c',  'v',  // 40-47
    'b',  'n',  'm',  ',',  '.',  '-',  0,    '*',  // 48-55
    0,    0,    0,    0,    0,    0,    0,    0,    // 64-71 (F5-F12)
    0,    0,    0,    0,    0,    0,    0,    0,    // 72-79 (numpad)
    0,    0,    0,    0,    0,    0,    0,    0,    // 80-87
};

/* Scancode table with Shift held, LATAM layout (ASCII subset) */
static const char scancode_to_ascii_shift_latam[] = {
    0,    0,    '!',  '"',  '#',  '$',  '%',  '&',  // 0-7
    '/',  '(',  ')',  '=',  '?',  '!',  0,    0,    // 8-15
    'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',  // 16-23
    'O',  'P',  '"',  '*',  0,    0,    'A',  'S',  // 24-31
    'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',  // 32-39
    '"',  '~',  0,    '|',  'Z',  'X',  'C',  'V',  // 40-47
    'B',  'N',  'M',  '<',  '>',  '_',  0,    '*',  // 48-55
    0,    0,    0,    0,    0,    0,    0,    0,    // 56-63
    0,    0,    0,    0,    0,    0,    0,    0,    // 64-71
    0,    0,    0,    0,    0,    0,    0,    0,    // 72-79
    0,    0,    0,    0,    0,    0,    0,    0,    // 80-87
};

static const char *keyboard_ascii_table_base(void)
{
    if (current_keyboard_layout == KEYBOARD_LAYOUT_LATAM)
        return scancode_to_ascii_latam;
    return scancode_to_ascii_us;
}

static const char *keyboard_ascii_table_shift(void)
{
    if (current_keyboard_layout == KEYBOARD_LAYOUT_LATAM)
        return scancode_to_ascii_shift_latam;
    return scancode_to_ascii_shift_us;
}

int keyboard_set_layout(int layout)
{
    if (layout != KEYBOARD_LAYOUT_US && layout != KEYBOARD_LAYOUT_LATAM)
        return -EINVAL;
    current_keyboard_layout = layout;
    return 0;
}

int keyboard_get_layout(void)
{
    return current_keyboard_layout;
}

const char *keyboard_get_layout_name(int layout)
{
    if (layout == KEYBOARD_LAYOUT_LATAM)
        return "latam";
    return "us";
}

char translate_scancode(uint8_t sc)
{
    switch (sc)
    {
    case 0x0E:
        return '\b'; /* Backspace */
    case 0x0F:
        return '\t'; /* Tab */
    case 0x1C:
        return '\n'; /* Enter */
    case 0x39:
        return ' '; /* Space */

    case 0x1D:
        ctrl_pressed = 1;
        return 0;
    case 0x9D:
        ctrl_pressed = 0;
        return 0;

    /*
     * Ctrl+L: shell clears via ESC + KEY_ESC_CLEAR_SCREEN (same pattern as
     * Page Up/Down); IRQ path does not call into the debug shell.
     */
    case 0x26:
        if (!ctrl_pressed)
            return 'l';
        keyboard_buffer_add(0x1B);
        keyboard_buffer_add((char)KEY_ESC_CLEAR_SCREEN);
        ctrl_pressed = 0;
        return 0;

    case 0x1E:
        if (ctrl_pressed)
            return 0x01; /* Ctrl+A */
        return shift_pressed ? keyboard_ascii_table_shift()[sc] : keyboard_ascii_table_base()[sc];
    case 0x12:
        if (ctrl_pressed)
            return 0x05; /* Ctrl+E */
        return shift_pressed ? keyboard_ascii_table_shift()[sc] : keyboard_ascii_table_base()[sc];
    case 0x16:
        if (ctrl_pressed)
            return 0x15; /* Ctrl+U */
        return shift_pressed ? keyboard_ascii_table_shift()[sc] : keyboard_ascii_table_base()[sc];

    default:
        if (sc < sizeof(scancode_to_ascii_us))
        {
            const char *base = keyboard_ascii_table_base();
            const char *shift = keyboard_ascii_table_shift();
            if (shift_pressed)
                return shift[sc];
            return base[sc];
        }
        return 0;
    }
}



static void keyboard_buffer_add(char c)
{
    int next = (keyboard_buffer_head + 1) % KERNEL_KBD_RING_SIZE;
    if (next != keyboard_buffer_tail) 
    {
        keyboard_buffer[keyboard_buffer_head] = c;
        keyboard_buffer_head = next;
    }
}

#ifdef __x86_64__


char keyboard_buffer_get(void) 
{
    if (keyboard_buffer_head == keyboard_buffer_tail) 
    {
        return 0; 
    }


    char c = keyboard_buffer[keyboard_buffer_tail];
    keyboard_buffer_tail = (keyboard_buffer_tail + 1) % KERNEL_KBD_RING_SIZE;
    return c;
}

int keyboard_buffer_has_data(void) 
{
    return keyboard_buffer_head != keyboard_buffer_tail;
}



void keyboard_buffer_clear(void) 
{
    keyboard_buffer_head = 0;
    keyboard_buffer_tail = 0;
}
#endif

void keyboard_handler64(void) 
{
    uint8_t scancode = inb(PS2_DATA_PORT);
    
    if (scancode == 0xE0)
    {
        ext_scancode = 1;
        return;
    }
    if (ext_scancode)
    {
        ext_scancode = 0;
        uint16_t kc = ext_scancode_to_keycode[scancode & 0x7F];
        if (kc)
            input_event_push(EV_KEY, kc, (scancode < 0x80) ? 1 : 0);
        if (scancode < 0x80)
        {
            if (scancode == 0x48)
            {
                keyboard_buffer_add(0x1B);
                keyboard_buffer_add(KEY_ESC_HISTORY_UP);
            }
            else if (scancode == 0x50)
            {
                keyboard_buffer_add(0x1B);
                keyboard_buffer_add(KEY_ESC_HISTORY_DOWN);
            }
            else if (scancode == 0x4B)
            {
                keyboard_buffer_add(0x1B);
                keyboard_buffer_add(KEY_ESC_CURSOR_LEFT);
            }
            else if (scancode == 0x4D)
            {
                keyboard_buffer_add(0x1B);
                keyboard_buffer_add(KEY_ESC_CURSOR_RIGHT);
            }
            else if (scancode == 0x47)
            {
                keyboard_buffer_add(0x1B);
                keyboard_buffer_add(KEY_ESC_CURSOR_HOME);
            }
            else if (scancode == 0x4F)
            {
                keyboard_buffer_add(0x1B);
                keyboard_buffer_add(KEY_ESC_CURSOR_END);
            }
            else if (scancode == 0x53)
            {
                keyboard_buffer_add(0x1B);
                keyboard_buffer_add(KEY_ESC_DELETE_CHAR);
            }
            else if (scancode == 0x49)
            {
                keyboard_buffer_add(0x1B);
                keyboard_buffer_add(KEY_ESC_SCROLL_UP);
            }
            else if (scancode == 0x51)
            {
                keyboard_buffer_add(0x1B);
                keyboard_buffer_add(KEY_ESC_SCROLL_DOWN);
            }
        }
        stdin_wake_check();
        return;
    }

    if (scancode == 0x2A || scancode == 0x36)
        shift_pressed = 1;
    else if (scancode == 0xAA || scancode == 0xB6)
        shift_pressed = 0;

    uint16_t kc = scancode_to_keycode[scancode & 0x7F];
    if (kc)
        input_event_push(EV_KEY, kc, (scancode < 0x80) ? 1 : 0);

    if (scancode < 0x80)
    {
        char ascii = translate_scancode(scancode);
        if (ascii != 0)
            keyboard_buffer_add(ascii);
    }
    stdin_wake_check();
}



/*
 * keyboard_init - Reset keyboard buffer.
 * IRQ1 unmasking is done centrally in main.c after PIC remap.
 */
void keyboard_init(void)
{
    keyboard_buffer_head = 0;
    keyboard_buffer_tail = 0;
    if (CONFIG_KEYBOARD_LAYOUT == KEYBOARD_LAYOUT_LATAM)
        current_keyboard_layout = KEYBOARD_LAYOUT_LATAM;
    else
        current_keyboard_layout = KEYBOARD_LAYOUT_US;
}


void set_idle_mode(int is_idle) 
{
    system_in_idle_mode = is_idle;
    if (is_idle) 
    {
        wake_requested = 0; // Reset wake request when entering idle
    }
}

int is_in_idle_mode(void) 
{
    return system_in_idle_mode;
}

void wakeup_from_idle(void) 
{
    if (system_in_idle_mode) {
        wake_requested = 1;
        system_in_idle_mode = 0;
      
    } else {
        print_colored("DEBUG: wakeup called but not in idle mode\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
    }
}

int is_wake_requested(void) 
{
    return wake_requested;
}

void clear_wake_request(void) 
{
    wake_requested = 0;
}
