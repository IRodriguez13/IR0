/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: dbgshell.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - Debug Shell (OSDev-inspired)
 * Copyright (C) 2025 Iván Rodriguez
 *
 * Debug shell that executes commands from debug_bins/.
 * Runs in kernel mode; all I/O via syscalls (SYS_READ, SYS_WRITE).
 * Inspired by OSDev shell but adapted for kernel-mode + syscall-direct usage.
 */

#include "dbgshell.h"
#include "debug_bins.h"
#include <ir0/syscall.h>
#include <ir0/fcntl.h>
#include <ir0/time.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define INPUT_MAX 255
#define BATCH_READ 32
#define EMPTY_READ_SPIN_MAX 100
#define SHELL_SCROLL_STEP 12

/* Write to stdout (fd 1) - all output goes through typewriter for consistent cursor */
static void write_stdout(const char *str)
{
    if (str)
        syscall(SYS_WRITE, 1, (uint64_t)str, strlen(str));
}

/* Write to stderr (fd 2) */
static void write_stderr(const char *str)
{
    if (str)
        syscall(SYS_WRITE, 2, (uint64_t)str, strlen(str));
}

/*
 * Bash-style prompt: ir0:<cwd>$  when getcwd works, else ir0$ .
 * Keeps cwd in the kernel-provided buffer (bounded by sizeof cwd).
 */
static void print_prompt(void)
{
    char cwd[256];
    int64_t r = syscall(SYS_GETCWD, (uint64_t)cwd, sizeof(cwd), 0);

    if (r >= 0)
    {
        write_stdout("ir0:");
        write_stdout(cwd);
        write_stdout("$ ");
    }
    else
        write_stdout("ir0$ ");
}

/* Echo single char to stdout - keeps typewriter/cursor in sync (OSDev-style) */
static void echo_char(char c)
{
    syscall(SYS_WRITE, 1, (uint64_t)&c, 1);
}

/* Sanitize input: remove control chars except tab, newline, carriage return (OSDev) */
static void sanitize_line(char *line)
{
    char *read = line;
    char *write = line;

    while (*read)
    {
        if ((*read >= 0x20 && *read < 0x7F) || *read == '\t' || *read == '\n' || *read == '\r')
            *write++ = *read;
        read++;
    }
    *write = '\0';
}

/* Check if line is empty or whitespace-only (OSDev) */
static bool is_empty_line(const char *line)
{
    while (*line)
    {
        if (*line != ' ' && *line != '\t' && *line != '\n' && *line != '\r')
            return false;
        line++;
    }
    return true;
}

/* Parse args to argc/argv - wrapper for debug_parse_args */
static int parse_args(char *line, int *argc_out, char **argv_out, int max_args)
{
    return debug_parse_args(line, argc_out, argv_out, max_args);
}

static int shell_starts_with(const char *text, const char *prefix)
{
    while (*prefix)
    {
        if (*text++ != *prefix++)
            return 0;
    }
    return 1;
}

static int shell_contains_space_or_tab(const char *line, int len)
{
    for (int i = 0; i < len; i++)
    {
        if (line[i] == ' ' || line[i] == '\t')
            return 1;
    }
    return 0;
}

static int shell_common_prefix_len(const char *a, const char *b)
{
    int n = 0;

    while (a[n] && b[n] && a[n] == b[n])
        n++;
    return n;
}

static int shell_collect_matches(const char *prefix, const char **out_matches, int max_matches, int list_matches)
{
    int count = 0;
    const char *builtins[] = { "help", "clear", "exit", NULL };
    extern struct debug_command *debug_commands[];

    for (int i = 0; builtins[i] != NULL; i++)
    {
        if (shell_starts_with(builtins[i], prefix))
        {
            if (count < max_matches)
                out_matches[count] = builtins[i];
            count++;
        }
    }

    for (int i = 0; debug_commands[i] != NULL; i++)
    {
        const char *name = debug_commands[i]->name;
        if (shell_starts_with(name, prefix))
        {
            if (count < max_matches)
                out_matches[count] = name;
            count++;
        }
    }

    if (list_matches && count > 1)
    {
        write_stdout("\n");
        for (int i = 0; i < count && i < max_matches; i++)
        {
            write_stdout(out_matches[i]);
            write_stdout("  ");
        }
        write_stdout("\n");
    }

    return count;
}

static void shell_try_tab_complete(char *input, int *input_pos)
{
    if (!input || !input_pos || *input_pos <= 0)
        return;
    if (shell_contains_space_or_tab(input, *input_pos))
        return;

    char prefix[INPUT_MAX + 1];
    int prefix_len = *input_pos;
    if (prefix_len > INPUT_MAX)
        prefix_len = INPUT_MAX;

    memcpy(prefix, input, (size_t)prefix_len);
    prefix[prefix_len] = '\0';

    const int max_matches = 128;
    const char *matches[128];
    int match_count = shell_collect_matches(prefix, matches, max_matches, 0);

    if (match_count <= 0)
        return;

    int common_len = (int)strlen(matches[0]);
    for (int i = 1; i < match_count && i < max_matches; i++)
    {
        int n = shell_common_prefix_len(matches[0], matches[i]);
        if (n < common_len)
            common_len = n;
    }

    if (match_count == 1)
    {
        const char *full = matches[0];
        int full_len = (int)strlen(full);
        for (int i = prefix_len; i < full_len && *input_pos < INPUT_MAX; i++)
        {
            input[(*input_pos)++] = full[i];
            echo_char(full[i]);
        }
        if (*input_pos < INPUT_MAX)
        {
            input[(*input_pos)++] = ' ';
            echo_char(' ');
        }
        input[*input_pos] = '\0';
        return;
    }

    if (common_len > prefix_len)
    {
        for (int i = prefix_len; i < common_len && *input_pos < INPUT_MAX; i++)
        {
            input[(*input_pos)++] = matches[0][i];
            echo_char(matches[0][i]);
        }
        input[*input_pos] = '\0';
        return;
    }

    shell_collect_matches(prefix, matches, max_matches, 1);
    print_prompt();
    write_stdout(input);
}

/* Execute a single command */
static void execute_single_command(const char *cmd_line)
{
    if (!cmd_line || cmd_line[0] == '\0')
        return;

    char buf[512];
    size_t i = 0;
    const char *p = cmd_line;

    while (i < sizeof(buf) - 1 && *p && *p != '\n' && *p != '|')
        buf[i++] = *p++;
    buf[i] = '\0';

    int argc = 0;
    char *argv[64];
    if (parse_args(buf, &argc, argv, 64) < 0 || argc == 0)
        return;

    const char *cmd_name = argv[0];

    if (strcmp(cmd_name, "help") == 0)
    {
        write_stdout("IR0 DebShell - Available commands:\n");
        write_stdout("  (All commands from debug_bins/)\n\n");
        extern struct debug_command *debug_commands[];
        for (int i = 0; debug_commands[i] != NULL; i++)
        {
            char line[256];
            int len = snprintf(line, sizeof(line), "  %s - %s\n",
                              debug_commands[i]->usage,
                              debug_commands[i]->description);
            if (len > 0 && len < (int)sizeof(line))
                write_stdout(line);
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
        syscall(SYS_EXIT, 0, 0, 0);
        __builtin_unreachable();
    }

    struct debug_command *cmd = debug_find_command(cmd_name);
    if (cmd)
    {
        int ret = cmd->handler(argc, argv);

        if (ret != 0)
        {
            char eb[160];
            int len = snprintf(eb, sizeof(eb), "ir0: %s: failed (exit %d)\n",
                               cmd_name, ret);

            if (len > 0 && len < (int)sizeof(eb))
                write_stderr(eb);
        }
        return;
    }

    write_stderr("Unknown command: ");
    write_stderr(cmd_name);
    write_stderr("\nType 'help' for available commands\n");
}

/* Execute command (handles pipes) */
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

    sanitize_line(cmd_copy);

    if (is_empty_line(cmd_copy))
        return;

    char *pipe_pos = strchr(cmd_copy, '|');
    if (!pipe_pos)
    {
        execute_single_command(cmd_copy);
        return;
    }

    *pipe_pos = '\0';
    char *first_cmd = cmd_copy;
    char *second_cmd = pipe_pos + 1;

    while (*first_cmd == ' ' || *first_cmd == '\t')
        first_cmd++;
    while (*second_cmd == ' ' || *second_cmd == '\t')
        second_cmd++;

    if (*first_cmd == '\0' || *second_cmd == '\0')
    {
        write_stderr("Invalid pipe syntax\n");
        return;
    }

    int pipefd[2];
    if (syscall(SYS_PIPE, (uint64_t)pipefd, 0, 0) != 0)
    {
        write_stderr("pipe() failed\n");
        return;
    }

    int pid_left = syscall(SYS_FORK, 0, 0, 0);
    if (pid_left < 0)
    {
        write_stderr("fork() failed\n");
        syscall(SYS_CLOSE, pipefd[0], 0, 0);
        syscall(SYS_CLOSE, pipefd[1], 0, 0);
        return;
    }

    if (pid_left == 0)
    {
        syscall(SYS_DUP2, pipefd[1], 1, 0);
        syscall(SYS_CLOSE, pipefd[0], 0, 0);
        syscall(SYS_CLOSE, pipefd[1], 0, 0);
        execute_single_command(first_cmd);
        syscall(SYS_EXIT, 0, 0, 0);
        __builtin_unreachable();
    }

    int pid_right = syscall(SYS_FORK, 0, 0, 0);
    if (pid_right < 0)
    {
        syscall(SYS_CLOSE, pipefd[0], 0, 0);
        syscall(SYS_CLOSE, pipefd[1], 0, 0);
        syscall(SYS_WAITPID, pid_left, 0, 0);
        write_stderr("fork() failed\n");
        return;
    }

    if (pid_right == 0)
    {
        syscall(SYS_DUP2, pipefd[0], 0, 0);
        syscall(SYS_CLOSE, pipefd[0], 0, 0);
        syscall(SYS_CLOSE, pipefd[1], 0, 0);
        execute_single_command(second_cmd);
        syscall(SYS_EXIT, 0, 0, 0);
        __builtin_unreachable();
    }

    syscall(SYS_CLOSE, pipefd[0], 0, 0);
    syscall(SYS_CLOSE, pipefd[1], 0, 0);
    syscall(SYS_WAITPID, pid_left, 0, 0);
    syscall(SYS_WAITPID, pid_right, 0, 0);
}

void cmd_clear(void)
{
    (void)syscall(SYS_CONSOLE_CLEAR, 0x0F, 0, 0);
    write_stdout("IR0 DebShell v0.0.1\nType 'help' for commands\n\n");
}

void vga_print(const char *str, uint8_t color)
{
    (void)color;
    if (!str)
        return;
    write_stdout(str);
}

void shell_entry(void)
{
    char input[INPUT_MAX + 1];
    int input_pos = 0;
    int pending_escape = 0;

    cmd_clear();

    for (;;)
    {
        print_prompt();

        input_pos = 0;
        input[0] = '\0';
        pending_escape = 0;

        {
            int empty_reads = 0;

            while (1)
            {
                char buf[BATCH_READ];
                int64_t n = syscall(SYS_READ, 0, (uint64_t)buf, sizeof(buf));

                if (n <= 0)
                {
                    empty_reads++;
                    if (empty_reads >= EMPTY_READ_SPIN_MAX)
                    {
                        struct timespec ts;

                        ts.tv_sec = 0;
                        ts.tv_nsec = 10000000L; /* 10 ms */
                        (void)ir0_nanosleep(&ts, NULL);
                        empty_reads = 0;
                    }
                    continue;
                }

                empty_reads = 0;

                for (int i = 0; i < n; i++)
                {
                    char c = buf[i];

                    if (pending_escape)
                    {
                        if (c == 0x01)
                        {
                            syscall(SYS_CONSOLE_SCROLL, SHELL_SCROLL_STEP, 0, 0);
                            pending_escape = 0;
                            continue;
                        }
                        else if (c == 0x02)
                        {
                            syscall(SYS_CONSOLE_SCROLL, -SHELL_SCROLL_STEP, 0, 0);
                            pending_escape = 0;
                            continue;
                        }
                        else if (c == 0x03)
                        {
                            cmd_clear();
                            pending_escape = 0;
                            continue;
                        }
                        pending_escape = 0;
                    }

                    if (c == 0x1B)
                    {
                        pending_escape = 1;
                        continue;
                    }

                    if (c == '\n' || c == '\r')
                    {
                        echo_char('\n');
                        input[input_pos] = '\0';
                        goto line_done;
                    }

                    if (c == '\t')
                    {
                        shell_try_tab_complete(input, &input_pos);
                        continue;
                    }

                    if (c == '\b' || c == 127)
                    {
                        if (input_pos > 0)
                        {
                            input_pos--;
                            echo_char('\b');
                        }
                    }
                    else if (c >= 32 && c < 127 && input_pos < INPUT_MAX)
                    {
                        input[input_pos++] = c;
                        echo_char(c);
                    }
                }
            }
        }
line_done:
        execute_command(input);
        /*
         * Tras cada línea, descartar descriptores >2 para que un fallo de
         * close en un comando no agote la tabla antes del siguiente.
         */
        debug_shell_sweep_open_fds();
    }
}
