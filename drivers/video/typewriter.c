/* SPDX-License-Identifier: GPL-3.0-only 
 * IR0 Kernel - Typewriter Effect
 * Copyright (C) 2025 Iván Rodriguez
 *
 * Typewriter/Teletype effect for console output
 */

#include "typewriter.h"
#include <ir0/vga.h>
#include <stdint.h>

/* VGA Text Mode Constants */
#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_BUFFER ((volatile uint16_t *)0xB8000)
#define SCROLLBACK_LINES 200

/* Global typewriter state */
static typewriter_mode_t current_mode = TYPEWRITER_FAST;
static int command_output_enabled = 1;

/*
 * Scrollback: circular buffer of lines for console scroll (Page Up/Down).
 * Each line is VGA_WIDTH cells (uint16_t). Logical line index L is at
 * scrollback[L % SCROLLBACK_LINES].
 */
static uint16_t scrollback[SCROLLBACK_LINES][VGA_WIDTH];
static size_t total_lines_written = 0;   /* Total lines ever appended */
static size_t current_col = 0;          /* Column in current line (0..VGA_WIDTH-1) */
static int scroll_offset = 0;           /* 0 = live at bottom; >0 = scrolled up */
static uint8_t scrollback_color = 0x0F;

/* Simple delay function using CPU cycles */
static void typewriter_delay(uint32_t microseconds)
{
    /* Approximate delay based on CPU cycles
     * This is a rough estimate - adjust multiplier as needed */
    volatile uint32_t cycles = microseconds * 1000; /* Rough approximation */
    
    for (volatile uint32_t i = 0; i < cycles; i++) {
        /* Busy wait */
        __asm__ volatile ("nop");
    }
}

/*
 * Redraw the visible 25 rows from scrollback. first_line is the logical
 * line index of the top row (can be negative; then top rows are blank).
 * Bottom row (row 24) shows line total_lines_written, so first_line = total_lines_written - 24.
 */
static void redraw_from_scrollback(void)
{
    extern int cursor_pos;
    int first_line = (int)((long)total_lines_written - (VGA_HEIGHT - 1) - scroll_offset);
    for (int row = 0; row < VGA_HEIGHT; row++)
    {
        int line_idx = first_line + row;
        uint16_t *dest = (uint16_t *)&VGA_BUFFER[row * VGA_WIDTH];
        if (line_idx < 0)
        {
            for (int col = 0; col < VGA_WIDTH; col++)
                dest[col] = (scrollback_color << 8) | ' ';
        }
        else
        {
            size_t buf_idx = (size_t)line_idx % SCROLLBACK_LINES;
            for (int col = 0; col < VGA_WIDTH; col++)
                dest[col] = scrollback[buf_idx][col];
        }
    }
    if (scroll_offset == 0)
    {
        int row = (total_lines_written == 0) ? 0 : (total_lines_written < (size_t)VGA_HEIGHT ? (int)total_lines_written : VGA_HEIGHT - 1);
        cursor_pos = row * VGA_WIDTH + (int)current_col;
    }
    else
        cursor_pos = (VGA_HEIGHT - 1) * VGA_WIDTH + (int)current_col;
}

void typewriter_console_scroll(int delta)
{
    /* Total lines on screen = total_lines_written (current line index) + 1 */
    int max_scroll = (int)(total_lines_written + 1) - VGA_HEIGHT;
    if (max_scroll < 0)
        max_scroll = 0;
    scroll_offset += delta;
    if (scroll_offset < 0)
        scroll_offset = 0;
    if (scroll_offset > max_scroll)
        scroll_offset = max_scroll;
    redraw_from_scrollback();
}

void typewriter_init(void)
{
    current_mode = TYPEWRITER_FAST;
    command_output_enabled = 1;
    total_lines_written = 0;
    current_col = 0;
    scroll_offset = 0;
    for (int i = 0; i < SCROLLBACK_LINES; i++)
        for (int c = 0; c < VGA_WIDTH; c++)
            scrollback[i][c] = (scrollback_color << 8) | ' ';
}

void typewriter_set_mode(typewriter_mode_t mode)
{
    current_mode = mode;
}

typewriter_mode_t typewriter_get_mode(void)
{
    return current_mode;
}

void typewriter_enable_for_commands(int enable)
{
    command_output_enabled = enable;
}

int typewriter_is_enabled_for_commands(void)
{
    return command_output_enabled;
}

void typewriter_print_char(char c)
{
    /* If typewriter is disabled, print normally */
    if (current_mode == TYPEWRITER_DISABLED || !command_output_enabled) {
        char temp[2] = {c, 0};
        print(temp);
        return;
    }
    
    /* Print the character */
    char temp[2] = {c, 0};
    print(temp);
    
    /* Add delay based on mode (except for newlines and spaces for better flow) */
    if (c != '\n' && c != ' ') {
        uint32_t delay;
        switch (current_mode) {
            case TYPEWRITER_FAST:
                delay = TYPEWRITER_DELAY_FAST;
                break;
            case TYPEWRITER_SLOW:
                delay = TYPEWRITER_DELAY_SLOW;
                break;
            case TYPEWRITER_NORMAL:
            default:
                delay = TYPEWRITER_DELAY_NORMAL;
                break;
        }
        typewriter_delay(delay);
    }
}

void typewriter_print(const char *str)
{
    if (!str) return;
    
    /* If typewriter is disabled, print normally */
    if (current_mode == TYPEWRITER_DISABLED || !command_output_enabled) {
        print(str);
        return;
    }
    
    /* Print character by character with delay */
    while (*str) {
        typewriter_print_char(*str);
        str++;
    }
}

void typewriter_print_uint32(uint32_t num)
{
    char buffer[16];
    int pos = 0;
    
    /* Handle zero case */
    if (num == 0) {
        typewriter_print_char('0');
        return;
    }
    
    /* Convert number to string (reverse order) */
    while (num > 0) {
        buffer[pos++] = '0' + (num % 10);
        num /= 10;
    }
    
    /* Print in correct order */
    for (int i = pos - 1; i >= 0; i--) {
        typewriter_print_char(buffer[i]);
    }
}

/* VGA typewriter functions for shell.
 * VGA and cursor_pos behave like before: we draw at cursor_pos and advance it;
 * we never reset cursor_pos from scrollback state, so shell echo (vga_putchar) is preserved.
 * Scrollback is updated in parallel for Page Up/Down only.
 */
void typewriter_vga_print_char(char c, uint8_t color)
{
    extern int cursor_pos;
    scrollback_color = color;

    if (scroll_offset > 0)
    {
        scroll_offset = 0;
        redraw_from_scrollback();
    }

    /* Update scrollback for SYS_CONSOLE_SCROLL (Page Up/Down) */
    {
        size_t line_idx = total_lines_written % SCROLLBACK_LINES;
        if (c == '\n')
        {
            for (size_t j = current_col; j < (size_t)VGA_WIDTH; j++)
                scrollback[line_idx][j] = (color << 8) | ' ';
            total_lines_written++;
            current_col = 0;
        }
        else if (c == '\b')
        {
            if (current_col > 0)
            {
                current_col--;
                scrollback[line_idx][current_col] = (color << 8) | ' ';
            }
        }
        else
        {
            scrollback[line_idx][current_col] = (color << 8) | (uint8_t)c;
            current_col++;
            if (current_col >= (size_t)VGA_WIDTH)
            {
                current_col = 0;
                total_lines_written++;
            }
        }
    }

    /* VGA update: use cursor_pos only (original behavior), do not overwrite cursor_pos from scrollback */
    if (scroll_offset == 0)
    {
        if (c == '\n')
        {
            cursor_pos = (cursor_pos / VGA_WIDTH + 1) * VGA_WIDTH;
            if (cursor_pos >= VGA_WIDTH * VGA_HEIGHT)
            {
                for (int i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++)
                    VGA_BUFFER[i] = VGA_BUFFER[i + VGA_WIDTH];
                for (int i = (VGA_HEIGHT - 1) * VGA_WIDTH; i < VGA_HEIGHT * VGA_WIDTH; i++)
                    VGA_BUFFER[i] = (color << 8) | ' ';
                cursor_pos = (VGA_HEIGHT - 1) * VGA_WIDTH;
            }
        }
        else if (c == '\b')
        {
            if (cursor_pos > 0)
            {
                cursor_pos--;
                VGA_BUFFER[cursor_pos] = (color << 8) | ' ';
            }
        }
        else
        {
            VGA_BUFFER[cursor_pos] = (color << 8) | (uint8_t)c;
            cursor_pos++;
            if (cursor_pos >= VGA_WIDTH * VGA_HEIGHT)
            {
                for (int i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++)
                    VGA_BUFFER[i] = VGA_BUFFER[i + VGA_WIDTH];
                for (int i = (VGA_HEIGHT - 1) * VGA_WIDTH; i < VGA_HEIGHT * VGA_WIDTH; i++)
                    VGA_BUFFER[i] = (color << 8) | ' ';
                cursor_pos = (VGA_HEIGHT - 1) * VGA_WIDTH;
            }
        }
    }

    if (current_mode != TYPEWRITER_DISABLED && command_output_enabled &&
        c != '\n' && c != '\b' && c != ' ')
    {
        uint32_t delay;
        switch (current_mode) {
            case TYPEWRITER_FAST:  delay = TYPEWRITER_DELAY_FAST;  break;
            case TYPEWRITER_SLOW:  delay = TYPEWRITER_DELAY_SLOW;  break;
            default:               delay = TYPEWRITER_DELAY_NORMAL; break;
        }
        typewriter_delay(delay);
    }
}

void typewriter_vga_print(const char *str, uint8_t color)
{
    if (!str) return;
    
    while (*str) {
        typewriter_vga_print_char(*str, color);
        str++;
    }
}