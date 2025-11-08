/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - Typewriter Effect
 * Copyright (C) 2025 Iván Rodriguez
 *
 * Typewriter/Teletype effect for console output
 */

#include "typewriter.h"
#include <ir0/print.h>
#include <stdint.h>

/* VGA Text Mode Constants */
#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_BUFFER ((volatile uint16_t *)0xB8000)

/* Global typewriter state */
static typewriter_mode_t current_mode = TYPEWRITER_FAST;
static int command_output_enabled = 1;

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

void typewriter_init(void)
{
    current_mode = TYPEWRITER_FAST;
    command_output_enabled = 1;
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

/* VGA typewriter functions for shell */
void typewriter_vga_print_char(char c, uint8_t color)
{
    extern int cursor_pos; /* Defined in shell.c */
    
    /* If typewriter is disabled, write directly to VGA */
    if (current_mode == TYPEWRITER_DISABLED || !command_output_enabled) {
        if (c == '\n') {
            cursor_pos = (cursor_pos / VGA_WIDTH + 1) * VGA_WIDTH;
            if (cursor_pos >= VGA_WIDTH * VGA_HEIGHT) {
                /* Scroll screen */
                for (int i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++)
                    VGA_BUFFER[i] = VGA_BUFFER[i + VGA_WIDTH];
                for (int i = (VGA_HEIGHT - 1) * VGA_WIDTH; i < VGA_HEIGHT * VGA_WIDTH; i++)
                    VGA_BUFFER[i] = 0x0F20;
                cursor_pos = (VGA_HEIGHT - 1) * VGA_WIDTH;
            }
        } else if (c == '\b') {
            if (cursor_pos > 0) {
                cursor_pos--;
                VGA_BUFFER[cursor_pos] = (color << 8) | ' ';
            }
        } else {
            VGA_BUFFER[cursor_pos] = (color << 8) | c;
            cursor_pos++;
            if (cursor_pos >= VGA_WIDTH * VGA_HEIGHT) {
                /* Scroll screen */
                for (int i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++)
                    VGA_BUFFER[i] = VGA_BUFFER[i + VGA_WIDTH];
                for (int i = (VGA_HEIGHT - 1) * VGA_WIDTH; i < VGA_HEIGHT * VGA_WIDTH; i++)
                    VGA_BUFFER[i] = 0x0F20;
                cursor_pos = (VGA_HEIGHT - 1) * VGA_WIDTH;
            }
        }
        return;
    }
    
    /* Write character with typewriter effect */
    if (c == '\n') {
        cursor_pos = (cursor_pos / VGA_WIDTH + 1) * VGA_WIDTH;
        if (cursor_pos >= VGA_WIDTH * VGA_HEIGHT) {
            /* Scroll screen */
            for (int i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++)
                VGA_BUFFER[i] = VGA_BUFFER[i + VGA_WIDTH];
            for (int i = (VGA_HEIGHT - 1) * VGA_WIDTH; i < VGA_HEIGHT * VGA_WIDTH; i++)
                VGA_BUFFER[i] = 0x0F20;
            cursor_pos = (VGA_HEIGHT - 1) * VGA_WIDTH;
        }
    } else if (c == '\b') {
        if (cursor_pos > 0) {
            cursor_pos--;
            VGA_BUFFER[cursor_pos] = (color << 8) | ' ';
        }
    } else {
        VGA_BUFFER[cursor_pos] = (color << 8) | c;
        cursor_pos++;
        if (cursor_pos >= VGA_WIDTH * VGA_HEIGHT) {
            /* Scroll screen */
            for (int i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++)
                VGA_BUFFER[i] = VGA_BUFFER[i + VGA_WIDTH];
            for (int i = (VGA_HEIGHT - 1) * VGA_WIDTH; i < VGA_HEIGHT * VGA_WIDTH; i++)
                VGA_BUFFER[i] = 0x0F20;
            cursor_pos = (VGA_HEIGHT - 1) * VGA_WIDTH;
        }
        
        /* Add delay for typewriter effect (except for newlines and spaces) */
        if (c != ' ') {
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
}

void typewriter_vga_print(const char *str, uint8_t color)
{
    if (!str) return;
    
    while (*str) {
        typewriter_vga_print_char(*str, color);
        str++;
    }
}