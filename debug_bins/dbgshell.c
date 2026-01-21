/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - Debug Shell (Minimal Executor)
 * Copyright (C) 2025 Iván Rodriguez
 *
 * Minimal debug shell that executes commands from debug_bins/
 * All I/O operations use syscalls only (simulates ring 3 behavior)
 */

#include "dbgshell.h"
#include "debug_bins.h"
#include <ir0/syscall.h>
#include <ir0/fcntl.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* VGA helpers (used by other kernel components) */
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_BUFFER ((volatile uint16_t *)0xB8000)

int cursor_pos = 0;

static void vga_putchar(char c, uint8_t color)
{
    if (c == '\n')
    {
        cursor_pos = (cursor_pos / VGA_WIDTH + 1) * VGA_WIDTH;
        if (cursor_pos >= VGA_WIDTH * VGA_HEIGHT)
        {
            /* Scroll screen */
            for (int i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++)
                VGA_BUFFER[i] = VGA_BUFFER[i + VGA_WIDTH];
            for (int i = (VGA_HEIGHT - 1) * VGA_WIDTH; i < VGA_HEIGHT * VGA_WIDTH; i++)
                VGA_BUFFER[i] = 0x0F20;
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
        VGA_BUFFER[cursor_pos] = (color << 8) | c;
        cursor_pos++;
        if (cursor_pos >= VGA_WIDTH * VGA_HEIGHT)
        {
            /* Scroll screen */
            for (int i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++)
                VGA_BUFFER[i] = VGA_BUFFER[i + VGA_WIDTH];
            for (int i = (VGA_HEIGHT - 1) * VGA_WIDTH; i < VGA_HEIGHT * VGA_WIDTH; i++)
                VGA_BUFFER[i] = 0x0F20;
            cursor_pos = (VGA_HEIGHT - 1) * VGA_WIDTH;
        }
    }
}

void vga_print(const char *str, uint8_t color)
{
    while (*str)
        vga_putchar(*str++, color);
}

static void vga_clear(void)
{
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        VGA_BUFFER[i] = 0x0F20;
    cursor_pos = 0;
}

void cmd_clear(void)
{
    vga_clear();
    /* Show banner after clear - using syscalls */
    const char *banner = "IR0 DebShell v0.0.1 pre-release 1\nType 'help' for available commands\n\n";
    syscall(SYS_WRITE, 1, (uint64_t)banner, strlen(banner));
}

/* Helper to write to stdout using syscall */
static void write_stdout(const char *str)
{
    if (str)
    {
        syscall(SYS_WRITE, 1, (uint64_t)str, strlen(str));
    }
}

/* Helper to write to stderr using syscall */
static void write_stderr(const char *str)
{
    if (str)
    {
        syscall(SYS_WRITE, 2, (uint64_t)str, strlen(str));
    }
}

/* Execute a single command using debug_bins/ */
static void execute_single_command(const char *cmd_line)
{
    if (!cmd_line || cmd_line[0] == '\0')
        return;
    
    char buf[512];
    size_t i = 0;
    const char *p = cmd_line;
    
    /* Copy command line, stop at newline or pipe */
    while (i < sizeof(buf) - 1 && *p && *p != '\n' && *p != '|')
        buf[i++] = *p++;
    buf[i] = '\0';
    
    /* Parse arguments to argc/argv format */
    char *argv[64];
    int argc = 0;
    if (debug_parse_args(buf, &argc, argv, 64) < 0 || argc == 0)
    {
        return;
    }
    
    const char *cmd_name = argv[0];
    
    /* Special built-in commands (shell functionality, not user commands) */
    if (strcmp(cmd_name, "help") == 0)
    {
        write_stdout("IR0 DebShell - Available commands:\n");
        write_stdout("  (All commands are loaded from debug_bins/)\n\n");
        
        /* List all registered commands - use extern from registry */
        extern struct debug_command *debug_commands[];
        for (int i = 0; debug_commands[i] != NULL; i++)
        {
            char line[256];
            int len = snprintf(line, sizeof(line), "  %s - %s\n",
                              debug_commands[i]->usage,
                              debug_commands[i]->description);
            if (len > 0 && len < (int)sizeof(line))
            {
                write_stdout(line);
            }
        }
        return;
    }
    
    if (strcmp(cmd_name, "clear") == 0)
    {
        cmd_clear();
        return;
    }
    
    if (strcmp(cmd_name, "exit") == 0)
    {
        /* Exit shell using syscall */
        syscall(SYS_EXIT, 0, 0, 0);
        __builtin_unreachable();
    }
    
    /* Try to find command in debug_bins/ */
    struct debug_command *cmd = debug_find_command(cmd_name);
    if (cmd)
    {
        /* Execute command using syscalls only (simulates ring 3) */
        int result = cmd->handler(argc, argv);
        (void)result;  /* Return code can be used later for error handling */
        return;
    }
    
    /* Command not found */
    write_stderr("Unknown command: ");
    write_stderr(cmd_name);
    write_stderr("\nType 'help' for available commands\n");
}

/* Execute command (handles pipes if needed) */
static void execute_command(const char *cmd)
{
    if (!cmd || *cmd == '\0')
        return;
    
    char cmd_copy[512];
    size_t len = 0;
    const char *src = cmd;
    while (len < sizeof(cmd_copy) - 1 && *src && *src != '\n')
        cmd_copy[len++] = *src++;
    cmd_copy[len] = '\0';
    
    /* Simple pipe support (can be enhanced later) */
    char *pipe_pos = strchr(cmd_copy, '|');
    if (!pipe_pos)
    {
        execute_single_command(cmd_copy);
        return;
    }
    
    *pipe_pos = '\0';
    char *first_cmd = cmd_copy;
    char *second_cmd = pipe_pos + 1;
    
    /* Skip whitespace */
    while (*first_cmd == ' ' || *first_cmd == '\t')
        first_cmd++;
    while (*second_cmd == ' ' || *second_cmd == '\t')
        second_cmd++;
    
    if (*first_cmd == '\0' || *second_cmd == '\0')
    {
        write_stderr("Invalid pipe syntax\n");
        return;
    }
    
    /* TODO: Implement proper pipe support (redirect stdout of first to stdin of second) */
    execute_single_command(first_cmd);
    execute_single_command(second_cmd);
}

/* Main shell entry point */
void shell_entry(void)
{
    char input[256];
    int input_pos = 0;
    
    vga_clear();
    write_stdout("IR0 DebShell v0.0.1 pre-release 1\n");
    write_stdout("Type 'help' for available commands\n\n");
    
    for (;;)
    {
        /* Print prompt using syscall */
        write_stdout("~$ ");
        
        /* Read input character by character using syscalls */
        input_pos = 0;
        while (1)
        {
            char c;
            int64_t n = syscall(SYS_READ, 0, (uint64_t)&c, 1);
            
            if (n <= 0)
                continue;
            
            if (c == '\n')
            {
                vga_putchar('\n', 0x0F);  /* Echo to VGA */
                input[input_pos] = '\0';
                break;
            }
            else if (c == '\b' || c == 127)  /* Backspace */
            {
                if (input_pos > 0)
                {
                    input_pos--;
                    vga_putchar('\b', 0x0F);  /* Echo to VGA */
                }
            }
            else if (c >= 32 && c < 127 && input_pos < 255)  /* Printable ASCII */
            {
                input[input_pos++] = c;
                vga_putchar(c, 0x0F);  /* Echo to VGA */
            }
        }
        
        /* Execute command */
        execute_command(input);
    }
}

