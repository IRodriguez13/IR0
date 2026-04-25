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
#include <ir0/stat.h>
#include <config.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define INPUT_MAX 255
#define BATCH_READ 32
#define EMPTY_READ_SPIN_MAX 100
#define SHELL_SCROLL_STEP 12

static uint32_t shell_pipe_seq = 0;

#define SHELL_HISTORY_MAX 32
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

static char shell_history[SHELL_HISTORY_MAX][INPUT_MAX + 1];
static int shell_history_count = 0;
static int shell_history_start = 0;

static const char *const shell_help_sections[] = {
    "shell", "core", "fs", "text", "identity", "diag", "net", "bt", NULL
};

static const char *const shell_help_flags[] = {
    "-s", "--section", "--sections", "-h", "--help", NULL
};

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

static void shell_write_nchar(char c, int count)
{
    for (int i = 0; i < count; i++)
        echo_char(c);
}

static void shell_cursor_left(int *cursor_idx)
{
    if (*cursor_idx <= 0)
        return;
    echo_char('\b');
    (*cursor_idx)--;
}

static void shell_cursor_right(const char *line_buf, int line_len, int *cursor_idx)
{
    if (*cursor_idx >= line_len)
        return;
    echo_char(line_buf[*cursor_idx]);
    (*cursor_idx)++;
}

static void shell_cursor_home(int *cursor_idx)
{
    while (*cursor_idx > 0)
        shell_cursor_left(cursor_idx);
}

static void shell_cursor_end(const char *line_buf, int line_len, int *cursor_idx)
{
    while (*cursor_idx < line_len)
        shell_cursor_right(line_buf, line_len, cursor_idx);
}

static void shell_clear_rendered_line(const char *line_buf, int line_len, int *cursor_idx)
{
    (void)line_buf;
    shell_cursor_home(cursor_idx);
    shell_write_nchar(' ', line_len);
    shell_write_nchar('\b', line_len);
}

static void shell_redraw_tail(const char *line_buf, int line_len, int repaint_from, int target_cursor)
{
    int tail_len = line_len - repaint_from;
    if (tail_len < 0)
        tail_len = 0;

    if (tail_len > 0)
        syscall(SYS_WRITE, 1, (uint64_t)&line_buf[repaint_from], (uint64_t)tail_len);

    echo_char(' ');
    shell_write_nchar('\b', tail_len + 1);
    for (int i = repaint_from; i < target_cursor && i < line_len; i++)
        echo_char(line_buf[i]);
}

static void shell_load_line(char *line_buf, int *line_len, int *cursor_idx, const char *src)
{
    shell_clear_rendered_line(line_buf, *line_len, cursor_idx);

    if (!src)
    {
        line_buf[0] = '\0';
        *line_len = 0;
        *cursor_idx = 0;
        return;
    }

    int n = (int)strlen(src);
    if (n > INPUT_MAX)
        n = INPUT_MAX;
    memcpy(line_buf, src, (size_t)n);
    line_buf[n] = '\0';
    *line_len = n;
    *cursor_idx = n;
    if (n > 0)
        syscall(SYS_WRITE, 1, (uint64_t)line_buf, (uint64_t)n);
}

static const char *shell_history_get(int logical_idx)
{
    if (logical_idx < 0 || logical_idx >= shell_history_count)
        return NULL;
    int ring_idx = (shell_history_start + logical_idx) % SHELL_HISTORY_MAX;
    return shell_history[ring_idx];
}

static void shell_history_add(const char *line)
{
    if (!line || !line[0])
        return;

    if (shell_history_count > 0)
    {
        const char *last = shell_history_get(shell_history_count - 1);
        if (last && strcmp(last, line) == 0)
            return;
    }

    if (shell_history_count < SHELL_HISTORY_MAX)
    {
        int idx = (shell_history_start + shell_history_count) % SHELL_HISTORY_MAX;
        strncpy(shell_history[idx], line, INPUT_MAX);
        shell_history[idx][INPUT_MAX] = '\0';
        shell_history_count++;
        return;
    }

    strncpy(shell_history[shell_history_start], line, INPUT_MAX);
    shell_history[shell_history_start][INPUT_MAX] = '\0';
    shell_history_start = (shell_history_start + 1) % SHELL_HISTORY_MAX;
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

struct linux_dirent64 {
    uint64_t d_ino;
    int64_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[];
};

#define DT_DIR 4

static int shell_starts_with(const char *text, const char *prefix)
{
    while (*prefix)
    {
        if (*text++ != *prefix++)
            return 0;
    }
    return 1;
}

static int shell_common_prefix_len(const char *a, const char *b)
{
    int n = 0;

    while (a[n] && b[n] && a[n] == b[n])
        n++;
    return n;
}

static int shell_is_whitespace(char c)
{
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r');
}

static void shell_get_token_context(const char *input, int input_pos, int *token_start, int *token_index)
{
    int in_token = 0;
    int current_start = input_pos;
    int idx = 0;

    for (int i = 0; i < input_pos; i++)
    {
        if (shell_is_whitespace(input[i]))
        {
            if (in_token)
            {
                in_token = 0;
                idx++;
            }
        }
        else if (!in_token)
        {
            in_token = 1;
            current_start = i;
        }
    }

    *token_start = in_token ? current_start : input_pos;
    *token_index = idx;
}

static int shell_extract_token(const char *input, int start, int end, char *out, int out_sz)
{
    int n = end - start;
    if (n < 0)
        n = 0;
    if (n >= out_sz)
        n = out_sz - 1;

    for (int i = 0; i < n; i++)
        out[i] = input[start + i];
    out[n] = '\0';
    return n;
}

static int shell_extract_first_token(const char *input, int input_pos, char *out, int out_sz)
{
    int start = -1;
    int end = -1;

    for (int i = 0; i < input_pos; i++)
    {
        if (!shell_is_whitespace(input[i]) && start < 0)
            start = i;
        if (start >= 0 && shell_is_whitespace(input[i]))
        {
            end = i;
            break;
        }
    }
    if (start < 0)
    {
        out[0] = '\0';
        return 0;
    }
    if (end < 0)
        end = input_pos;
    return shell_extract_token(input, start, end, out, out_sz);
}

static int shell_collect_command_matches(const char *prefix, const char **out_matches, int max_matches, int list_matches)
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

static struct debug_command *shell_find_command(const char *name)
{
    if (!name || name[0] == '\0')
        return NULL;
    return debug_find_command(name);
}

static int shell_collect_flags_matches(const struct debug_command *cmd, const char *prefix, const char **out_matches, int max_matches)
{
    int count = 0;
    if (!cmd || !cmd->flags)
        return 0;

    for (int i = 0; cmd->flags[i] != NULL; i++)
    {
        if (shell_starts_with(cmd->flags[i], prefix))
        {
            if (count < max_matches)
                out_matches[count] = cmd->flags[i];
            count++;
        }
    }
    return count;
}

static int shell_collect_value_matches(const char *cmd_name, const char *prefix, const char **out_matches, int max_matches)
{
    int count = 0;
    if (!cmd_name)
        return 0;

    if (strcmp(cmd_name, "help") == 0)
    {
        for (int i = 0; shell_help_sections[i] != NULL; i++)
        {
            if (shell_starts_with(shell_help_sections[i], prefix))
            {
                if (count < max_matches)
                    out_matches[count] = shell_help_sections[i];
                count++;
            }
        }
        return count;
    }

    if (strcmp(cmd_name, "keymap") == 0)
    {
        const char *values[] = { "us", "latam", NULL };
        for (int i = 0; values[i] != NULL; i++)
        {
            if (shell_starts_with(values[i], prefix))
            {
                if (count < max_matches)
                    out_matches[count] = values[i];
                count++;
            }
        }
    }
    return count;
}

static int shell_complete_from_matches(char *input, int *input_pos, int token_start, const char *token, const char **matches, int match_count, int append_space, int list_on_ambiguous)
{
    int token_len = (int)strlen(token);
    int common_len;

    if (match_count <= 0)
        return 0;

    common_len = (int)strlen(matches[0]);
    for (int i = 1; i < match_count; i++)
    {
        int n = shell_common_prefix_len(matches[0], matches[i]);
        if (n < common_len)
            common_len = n;
    }

    if (match_count == 1)
    {
        const char *full = matches[0];
        int full_len = (int)strlen(full);
        for (int i = token_len; i < full_len && *input_pos < INPUT_MAX; i++)
        {
            input[(*input_pos)++] = full[i];
            echo_char(full[i]);
        }
        if (append_space && *input_pos < INPUT_MAX)
        {
            input[(*input_pos)++] = ' ';
            echo_char(' ');
        }
        input[*input_pos] = '\0';
        return 1;
    }

    if (common_len > token_len)
    {
        for (int i = token_len; i < common_len && *input_pos < INPUT_MAX; i++)
        {
            input[(*input_pos)++] = matches[0][i];
            echo_char(matches[0][i]);
        }
        input[*input_pos] = '\0';
        return 1;
    }

    if (list_on_ambiguous)
    {
        write_stdout("\n");
        for (int i = 0; i < match_count; i++)
        {
            write_stdout(matches[i]);
            write_stdout("  ");
        }
        write_stdout("\n");
        print_prompt();
        input[*input_pos] = '\0';
        write_stdout(input);
    }

    (void)token_start;
    return 1;
}

static int shell_path_is_dir(const char *dir_path, const char *name, unsigned char d_type)
{
    if (d_type == DT_DIR)
        return 1;
    if (d_type != 0)
        return 0;

    char full[512];
    stat_t st;
    int len;
    if (strcmp(dir_path, "/") == 0)
        len = snprintf(full, sizeof(full), "/%s", name);
    else
        len = snprintf(full, sizeof(full), "%s/%s", dir_path, name);
    if (len <= 0 || len >= (int)sizeof(full))
        return 0;
    if (syscall(SYS_STAT, (uint64_t)full, (uint64_t)&st, 0) < 0)
        return 0;
    return S_ISDIR(st.st_mode) ? 1 : 0;
}

struct shell_pseudo_entry
{
    const char *path;
    int is_dir;
};

static int shell_pseudo_find_entry(const char *path, const struct shell_pseudo_entry *entries, int entry_count)
{
    for (int i = 0; i < entry_count; i++)
    {
        if (strcmp(entries[i].path, path) == 0)
            return i;
    }
    return -1;
}

static int shell_try_pseudofs_complete(char *input, int *input_pos, int token_start)
{
    static const struct shell_pseudo_entry pseudo_entries[] = {
        { "/proc/", 1 },
        { "/proc/pid/", 1 },
        { "/proc/self/", 1 },
        { "/proc/meminfo", 0 },
        { "/proc/ps", 0 },
        { "/proc/netinfo", 0 },
        { "/proc/net/", 1 },
        { "/proc/net/dev", 0 },
        { "/proc/drivers", 0 },
        { "/proc/uptime", 0 },
        { "/proc/version", 0 },
        { "/proc/cpuinfo", 0 },
        { "/proc/loadavg", 0 },
        { "/proc/filesystems", 0 },
        { "/proc/blockdevices", 0 },
        { "/proc/partitions", 0 },
        { "/proc/mounts", 0 },
        { "/proc/interrupts", 0 },
        { "/proc/iomem", 0 },
        { "/proc/ioports", 0 },
        { "/proc/modules", 0 },
        { "/proc/timer_list", 0 },
        { "/proc/kmsg", 0 },
        { "/proc/swaps", 0 },
#if CONFIG_ENABLE_BLUETOOTH
        { "/proc/bluetooth/", 1 },
        { "/proc/bluetooth/devices", 0 },
        { "/proc/bluetooth/scan", 0 },
#endif
        { "/sys/", 1 },
        { "/sys/kernel/", 1 },
        { "/sys/kernel/version", 0 },
        { "/sys/kernel/hostname", 0 },
        { "/sys/kernel/max_processes", 0 },
        { "/sys/devices/", 1 },
        { "/sys/devices/system", 0 },
        { "/sys/devices/system/", 1 },
        { "/sys/devices/system/cpu0", 0 },
        { "/sys/devices/system/cpu0/", 1 },
        { "/sys/devices/system/cpu0/online", 0 },
        { "/sys/devices/block", 0 },
        { "/sys/console/", 1 },
        { "/sys/console/mode", 0 },
#if CONFIG_ENABLE_BLUETOOTH
        { "/sys/class/", 1 },
        { "/sys/class/bluetooth/", 1 },
        { "/sys/class/bluetooth/hci0/", 1 },
        { "/sys/class/bluetooth/hci0/address", 0 },
        { "/sys/class/bluetooth/hci0/state", 0 },
        { "/sys/class/bluetooth/topology/", 1 },
        { "/sys/class/bluetooth/topology/neighbors", 0 },
        { "/sys/class/bluetooth/sessions", 0 },
#endif
        { "/dev/", 1 },
        { "/dev/null", 0 },
        { "/dev/zero", 0 },
        { "/dev/full", 0 },
        { "/dev/console", 0 },
        { "/dev/tty", 0 },
        { "/dev/stdin", 0 },
        { "/dev/stdout", 0 },
        { "/dev/stderr", 0 },
        { "/dev/serial", 0 },
        { "/dev/net", 0 },
        { "/dev/mouse", 0 },
        { "/dev/audio", 0 },
        { "/dev/kmsg", 0 },
        { "/dev/random", 0 },
        { "/dev/urandom", 0 },
        { "/dev/ipc", 0 },
        { "/dev/disk", 0 },
        { "/dev/fb0", 0 },
        { "/dev/events0", 0 },
        { "/dev/bluetooth/", 1 },
        { "/dev/bluetooth/hci0", 0 },
        { "/dev/hda", 0 },
        { "/dev/hda1", 0 },
        { "/dev/hda2", 0 },
        { "/dev/hda3", 0 },
        { "/dev/hda4", 0 },
        { "/dev/hdb", 0 },
        { "/dev/hdb1", 0 },
        { "/dev/hdb2", 0 },
        { "/dev/hdb3", 0 },
        { "/dev/hdb4", 0 },
        { "/dev/hdc", 0 },
        { "/dev/hdc1", 0 },
        { "/dev/hdc2", 0 },
        { "/dev/hdc3", 0 },
        { "/dev/hdc4", 0 },
        { "/dev/hdd", 0 },
        { "/dev/hdd1", 0 },
        { "/dev/hdd2", 0 },
        { "/dev/hdd3", 0 },
        { "/dev/hdd4", 0 },
    };
    const int entry_count = (int)(sizeof(pseudo_entries) / sizeof(pseudo_entries[0]));
    const char *matches[128];
    char token[INPUT_MAX + 1];
    int match_count = 0;

    shell_extract_token(input, token_start, *input_pos, token, sizeof(token));
    if (token[0] != '/')
        return 0;

    if (!(shell_starts_with("/proc", token) ||
          shell_starts_with("/sys", token) ||
          shell_starts_with("/dev", token) ||
          shell_starts_with(token, "/proc") ||
          shell_starts_with(token, "/sys") ||
          shell_starts_with(token, "/dev")))
    {
        return 0;
    }

    for (int i = 0; i < entry_count && match_count < 128; i++)
    {
        if (shell_starts_with(pseudo_entries[i].path, token))
        {
            matches[match_count++] = pseudo_entries[i].path;
        }
    }

    if (match_count <= 0)
        return 0;

    if (!shell_complete_from_matches(input, input_pos, token_start, token, matches, match_count, 0, 1))
        return 0;

    if (match_count == 1)
    {
        int idx = shell_pseudo_find_entry(matches[0], pseudo_entries, entry_count);
        int is_dir = (idx >= 0) ? pseudo_entries[idx].is_dir : 0;

        if (is_dir)
        {
            if (*input_pos > 0 && input[*input_pos - 1] != '/' && *input_pos < INPUT_MAX)
            {
                input[(*input_pos)++] = '/';
                echo_char('/');
                input[*input_pos] = '\0';
            }
        }
        else if (*input_pos < INPUT_MAX)
        {
            input[(*input_pos)++] = ' ';
            echo_char(' ');
            input[*input_pos] = '\0';
        }
    }

    return 1;
}

static int shell_complete_path(char *input, int *input_pos, int token_start)
{
    char token[INPUT_MAX + 1];
    char dir_path[256];
    char base_prefix[256];
    char name_prefix[256];
    char cwd[256];
    const char *matches[128];
    unsigned char match_types[128];
    int match_count = 0;
    int token_len = shell_extract_token(input, token_start, *input_pos, token, sizeof(token));
    char *slash = strrchr(token, '/');

    if (shell_try_pseudofs_complete(input, input_pos, token_start))
        return 1;

    if (slash)
    {
        int dir_len = (int)(slash - token);
        if (dir_len == 0)
        {
            strcpy(dir_path, "/");
            strcpy(base_prefix, "/");
        }
        else
        {
            if (dir_len >= (int)sizeof(dir_path))
                return 0;
            memcpy(dir_path, token, (size_t)dir_len);
            dir_path[dir_len] = '\0';
            memcpy(base_prefix, token, (size_t)(dir_len + 1));
            base_prefix[dir_len + 1] = '\0';
        }
        snprintf(name_prefix, sizeof(name_prefix), "%s", slash + 1);
    }
    else
    {
        if (syscall(SYS_GETCWD, (uint64_t)cwd, sizeof(cwd), 0) < 0)
            strcpy(cwd, "/");
        snprintf(dir_path, sizeof(dir_path), "%s", cwd);
        base_prefix[0] = '\0';
        snprintf(name_prefix, sizeof(name_prefix), "%s", token);
    }

    int fd = syscall(SYS_OPEN, (uint64_t)dir_path, O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0)
        return 0;

    char dirent_buf[1024];
    int64_t bytes_read;
    while ((bytes_read = syscall(SYS_GETDENTS, fd, (uint64_t)dirent_buf, sizeof(dirent_buf))) > 0)
    {
        size_t offset = 0;
        while (offset < (size_t)bytes_read && match_count < 128)
        {
            struct linux_dirent64 *dent = (struct linux_dirent64 *)(dirent_buf + offset);
            size_t reclen = (size_t)dent->d_reclen;
            const char *name = dent->d_name;

            if (reclen < sizeof(struct linux_dirent64) || offset + reclen > (size_t)bytes_read)
            {
                syscall(SYS_CLOSE, fd, 0, 0);
                return 0;
            }
            if (name[0] == '\0')
            {
                offset += reclen;
                continue;
            }
            if (name[0] == '.' && name_prefix[0] != '.')
            {
                offset += reclen;
                continue;
            }
            if (shell_starts_with(name, name_prefix))
            {
                static char storage[128][256];
                snprintf(storage[match_count], sizeof(storage[match_count]), "%s%s", base_prefix, name);
                matches[match_count] = storage[match_count];
                match_types[match_count] = dent->d_type;
                match_count++;
            }
            offset += reclen;
        }
    }
    syscall(SYS_CLOSE, fd, 0, 0);

    if (match_count <= 0)
        return 0;

    int completed = shell_complete_from_matches(input, input_pos, token_start, token, matches, match_count, 0, 1);
    if (!completed)
        return 0;

    if (match_count == 1)
    {
        const char *full_match = matches[0];
        const char *entry_name = strrchr(full_match, '/');
        if (!entry_name)
            entry_name = full_match;
        else
            entry_name++;
        if (shell_path_is_dir(dir_path, entry_name, match_types[0]))
        {
            if (*input_pos < INPUT_MAX && (*input_pos == 0 || input[*input_pos - 1] != '/'))
            {
                input[(*input_pos)++] = '/';
                echo_char('/');
                input[*input_pos] = '\0';
            }
        }
        else if (*input_pos < INPUT_MAX)
        {
            input[(*input_pos)++] = ' ';
            echo_char(' ');
            input[*input_pos] = '\0';
        }
    }

    (void)token_len;
    return 1;
}

static void shell_try_tab_complete(char *input, int *input_pos)
{
    if (!input || !input_pos)
        return;

    int token_start = 0;
    int token_index = 0;
    char token[INPUT_MAX + 1];
    char cmd_name[INPUT_MAX + 1];
    const char *matches[128];
    shell_get_token_context(input, *input_pos, &token_start, &token_index);
    shell_extract_token(input, token_start, *input_pos, token, sizeof(token));

    if (token_index == 0)
    {
        int match_count = shell_collect_command_matches(token, matches, 128, 0);
        if (match_count <= 0)
            return;
        (void)shell_complete_from_matches(input, input_pos, token_start, token, matches, match_count, 1, 1);
        return;
    }

    shell_extract_first_token(input, *input_pos, cmd_name, sizeof(cmd_name));

    if (strcmp(cmd_name, "help") == 0)
    {
        if (token[0] == '-')
        {
            int match_count = 0;
            for (int i = 0; shell_help_flags[i] != NULL && match_count < 128; i++)
            {
                if (shell_starts_with(shell_help_flags[i], token))
                    matches[match_count++] = shell_help_flags[i];
            }
            if (match_count > 0)
            {
                (void)shell_complete_from_matches(input, input_pos, token_start, token, matches, match_count, 1, 1);
                return;
            }
        }

        int value_match_count = shell_collect_value_matches(cmd_name, token, matches, 128);
        if (value_match_count > 0)
        {
            (void)shell_complete_from_matches(input, input_pos, token_start, token, matches, value_match_count, 1, 1);
            return;
        }
        return;
    }

    struct debug_command *cmd = shell_find_command(cmd_name);
    if (!cmd)
        return;

    if (token[0] == '-')
    {
        int match_count = shell_collect_flags_matches(cmd, token, matches, 128);
        if (match_count > 0)
        {
            (void)shell_complete_from_matches(input, input_pos, token_start, token, matches, match_count, 1, 1);
            return;
        }
    }

    int value_match_count = shell_collect_value_matches(cmd_name, token, matches, 128);
    if (value_match_count > 0)
    {
        (void)shell_complete_from_matches(input, input_pos, token_start, token, matches, value_match_count, 1, 1);
        return;
    }

    if (shell_complete_path(input, input_pos, token_start))
        return;
}

static int shell_help_section_matches(const char *filter, const char *section)
{
    if (!filter || !filter[0])
        return 1;
    if (!section)
        return 0;
    return strcmp(filter, section) == 0;
}

static void shell_print_help_sections(void)
{
    write_stdout("Help sections:\n");
    for (int i = 0; shell_help_sections[i] != NULL; i++)
    {
        write_stdout("  ");
        write_stdout(shell_help_sections[i]);
        write_stdout("\n");
    }
}

static void shell_print_help_filtered(const char *section_filter)
{
    extern struct debug_command *debug_commands[];
    int printed = 0;

    write_stdout("IR0 DebShell - Available commands:\n");
    if (section_filter && section_filter[0])
    {
        write_stdout("  section: ");
        write_stdout(section_filter);
        write_stdout("\n\n");
    }
    else
    {
        write_stdout("  (use: help -s <section> or help <section>)\n\n");
    }

    if (shell_help_section_matches(section_filter, "shell"))
    {
        write_stdout("[shell]\n");
        write_stdout("  help - Show command help (supports sections)\n");
        write_stdout("  clear - Clear console\n");
        write_stdout("  exit - Exit shell process\n");
        printed = 1;
    }

    for (int i = 0; shell_help_sections[i] != NULL; i++)
    {
        const char *sec = shell_help_sections[i];
        if (strcmp(sec, "shell") == 0)
            continue;
        if (!shell_help_section_matches(section_filter, sec))
            continue;

        int sec_printed = 0;
        for (int j = 0; debug_commands[j] != NULL; j++)
        {
            const char *cmd_sec = debug_command_section(debug_commands[j]->name);
            if (!cmd_sec || strcmp(cmd_sec, sec) != 0)
                continue;

            if (!sec_printed)
            {
                write_stdout("[");
                write_stdout(sec);
                write_stdout("]\n");
                sec_printed = 1;
            }

            char line[256];
            int len = snprintf(line, sizeof(line), "  %s - %s\n",
                               debug_commands[j]->usage,
                               debug_commands[j]->description);
            if (len > 0 && len < (int)sizeof(line))
                write_stdout(line);
        }
        if (sec_printed)
            printed = 1;
    }

    if (!printed)
    {
        write_stderr("help: no commands found for section '");
        write_stderr(section_filter ? section_filter : "");
        write_stderr("'\n");
    }
}

static int shell_handle_help(int argc, char **argv)
{
    const char *section = NULL;

    if (argc == 1)
    {
        shell_print_help_filtered(NULL);
        return 0;
    }

    for (int i = 1; i < argc; i++)
    {
        const char *arg = argv[i];
        if (strcmp(arg, "--sections") == 0)
        {
            shell_print_help_sections();
            return 0;
        }
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0)
        {
            write_stdout("usage: help [section]\n");
            write_stdout("       help -s <section>\n");
            write_stdout("       help --sections\n");
            return 0;
        }
        if (strcmp(arg, "-s") == 0 || strcmp(arg, "--section") == 0)
        {
            if (i + 1 >= argc)
            {
                write_stderr("help: missing section after -s/--section\n");
                return 1;
            }
            section = argv[++i];
            continue;
        }

        if (arg[0] == '-')
        {
            write_stderr("help: unknown option '");
            write_stderr(arg);
            write_stderr("'\n");
            return 1;
        }

        if (!section)
            section = arg;
        else
        {
            write_stderr("help: too many section arguments\n");
            return 1;
        }
    }

    if (section && !debug_is_valid_section(section))
    {
        write_stderr("help: unknown section '");
        write_stderr(section);
        write_stderr("'\n");
        write_stderr("hint: run 'help --sections'\n");
        return 1;
    }

    shell_print_help_filtered(section);
    return 0;
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
        (void)shell_handle_help(argc, argv);
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

    /*
     * Current SYS_FORK does not provide full POSIX fork semantics yet.
     * Emulate multi-stage pipelines in-process using temporary spool files:
     * cmd1 | cmd2 | cmd3 ... (stages chained through spool fd rewind).
     */
    char *stages[16];
    int stage_count = 0;
    char *cursor = cmd_copy;
    while (cursor && stage_count < (int)(sizeof(stages) / sizeof(stages[0])))
    {
        char *sep = strchr(cursor, '|');
        if (sep)
            *sep = '\0';

        while (*cursor == ' ' || *cursor == '\t')
            cursor++;

        char *end = cursor + strlen(cursor);
        while (end > cursor && (end[-1] == ' ' || end[-1] == '\t'))
        {
            end[-1] = '\0';
            end--;
        }

        if (*cursor == '\0')
        {
            write_stderr("Invalid pipe syntax\n");
            return;
        }

        stages[stage_count++] = cursor;
        cursor = sep ? (sep + 1) : NULL;
    }

    if (cursor != NULL || stage_count < 2)
    {
        write_stderr("Too many pipe stages\n");
        return;
    }

    int saved_stdin = syscall(SYS_DUP, 0, 0, 0);
    int saved_stdout = syscall(SYS_DUP, 1, 0, 0);
    if (saved_stdin < 0 || saved_stdout < 0)
    {
        write_stderr("pipe setup failed\n");
        if (saved_stdin >= 0)
            syscall(SYS_CLOSE, saved_stdin, 0, 0);
        if (saved_stdout >= 0)
            syscall(SYS_CLOSE, saved_stdout, 0, 0);
        return;
    }

    int input_fd = -1;
    char input_path[96];
    int input_path_valid = 0;
    int had_error = 0;
    int64_t pid = syscall(SYS_GETPID, 0, 0, 0);
    uint64_t upid = (pid < 0) ? 0 : (uint64_t)pid;
    char pid_str[32];
    char cwd[256];
    debug_u64_to_dec(upid, pid_str, sizeof(pid_str));
    if (syscall(SYS_GETCWD, (uint64_t)cwd, sizeof(cwd), 0) < 0)
        strcpy(cwd, "/");

    for (int i = 0; i < stage_count; i++)
    {
        int is_last = (i == stage_count - 1);
        int output_fd = -1;
        char spool_path[96];

        if (!is_last)
        {
            uint32_t seq = ++shell_pipe_seq;
            int n;
            if (strcmp(cwd, "/") == 0)
                n = snprintf(spool_path, sizeof(spool_path), "/.dbgshell_pipe_%s_%u_%d",
                             pid_str, (unsigned)seq, i);
            else
                n = snprintf(spool_path, sizeof(spool_path), "%s/.dbgshell_pipe_%s_%u_%d",
                             cwd, pid_str, (unsigned)seq, i);
            if (n > 0 && n < (int)sizeof(spool_path))
                output_fd = syscall(SYS_OPEN, (uint64_t)spool_path, O_CREAT | O_TRUNC | O_RDWR, 0600);
            if (output_fd < 0)
            {
                n = snprintf(spool_path, sizeof(spool_path), "/tmp/.dbgshell_pipe_%s_%u_%d",
                             pid_str, (unsigned)seq, i);
                if (n > 0 && n < (int)sizeof(spool_path))
                    output_fd = syscall(SYS_OPEN, (uint64_t)spool_path, O_CREAT | O_TRUNC | O_RDWR, 0600);
            }
            if (output_fd < 0)
            {
                n = snprintf(spool_path, sizeof(spool_path), "/.dbgshell_pipe_%s_%u_%d",
                             pid_str, (unsigned)seq, i);
                if (n > 0 && n < (int)sizeof(spool_path))
                    output_fd = syscall(SYS_OPEN, (uint64_t)spool_path, O_CREAT | O_TRUNC | O_RDWR, 0600);
            }
            if (output_fd < 0)
            {
                write_stderr("pipe setup failed\n");
                had_error = 1;
                break;
            }
        }

        if (input_fd >= 0)
        {
            if (syscall(SYS_DUP2, input_fd, 0, 0) < 0)
            {
                write_stderr("pipe redirection failed\n");
                if (output_fd >= 0)
                {
                    syscall(SYS_CLOSE, output_fd, 0, 0);
                    syscall(SYS_UNLINK, (uint64_t)spool_path, 0, 0);
                }
                had_error = 1;
                break;
            }
        }
        else
        {
            (void)syscall(SYS_DUP2, saved_stdin, 0, 0);
        }

        if (!is_last)
        {
            if (syscall(SYS_DUP2, output_fd, 1, 0) < 0)
            {
                write_stderr("pipe redirection failed\n");
                syscall(SYS_CLOSE, output_fd, 0, 0);
                syscall(SYS_UNLINK, (uint64_t)spool_path, 0, 0);
                had_error = 1;
                break;
            }
        }
        else
        {
            (void)syscall(SYS_DUP2, saved_stdout, 1, 0);
        }

        execute_single_command(stages[i]);

        if (input_fd >= 0)
        {
            syscall(SYS_CLOSE, input_fd, 0, 0);
            input_fd = -1;
            if (input_path_valid)
            {
                syscall(SYS_UNLINK, (uint64_t)input_path, 0, 0);
                input_path_valid = 0;
            }
        }

        if (!is_last)
        {
            (void)syscall(SYS_DUP2, saved_stdout, 1, 0);
            (void)syscall(SYS_LSEEK, output_fd, 0, SEEK_SET);
            input_fd = output_fd;
            strncpy(input_path, spool_path, sizeof(input_path) - 1);
            input_path[sizeof(input_path) - 1] = '\0';
            input_path_valid = 1;
        }
    }

    (void)syscall(SYS_DUP2, saved_stdin, 0, 0);
    (void)syscall(SYS_DUP2, saved_stdout, 1, 0);
    syscall(SYS_CLOSE, saved_stdin, 0, 0);
    syscall(SYS_CLOSE, saved_stdout, 0, 0);
    if (input_fd >= 0)
    {
        syscall(SYS_CLOSE, input_fd, 0, 0);
        if (input_path_valid)
            syscall(SYS_UNLINK, (uint64_t)input_path, 0, 0);
    }
    if (had_error)
        return;
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
    char line_buf[INPUT_MAX + 1];
    int line_len = 0;
    int cursor_idx = 0;
    int pending_escape = 0;

    cmd_clear();

    for (;;)
    {
        print_prompt();

        line_len = 0;
        cursor_idx = 0;
        line_buf[0] = '\0';
        pending_escape = 0;
        int history_nav = -1;
        char history_saved[INPUT_MAX + 1];
        history_saved[0] = '\0';

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
                        else if (c == KEY_ESC_HISTORY_UP)
                        {
                            if (shell_history_count > 0)
                            {
                                if (history_nav < 0)
                                {
                                    strncpy(history_saved, line_buf, INPUT_MAX);
                                    history_saved[INPUT_MAX] = '\0';
                                    history_nav = shell_history_count - 1;
                                }
                                else if (history_nav > 0)
                                {
                                    history_nav--;
                                }
                                const char *entry = shell_history_get(history_nav);
                                if (entry)
                                    shell_load_line(line_buf, &line_len, &cursor_idx, entry);
                            }
                            pending_escape = 0;
                            continue;
                        }
                        else if (c == KEY_ESC_HISTORY_DOWN)
                        {
                            if (history_nav >= 0)
                            {
                                if (history_nav < shell_history_count - 1)
                                {
                                    history_nav++;
                                    const char *entry = shell_history_get(history_nav);
                                    if (entry)
                                        shell_load_line(line_buf, &line_len, &cursor_idx, entry);
                                }
                                else
                                {
                                    history_nav = -1;
                                    shell_load_line(line_buf, &line_len, &cursor_idx, history_saved);
                                }
                            }
                            pending_escape = 0;
                            continue;
                        }
                        else if (c == KEY_ESC_CURSOR_LEFT)
                        {
                            shell_cursor_left(&cursor_idx);
                            pending_escape = 0;
                            continue;
                        }
                        else if (c == KEY_ESC_CURSOR_RIGHT)
                        {
                            shell_cursor_right(line_buf, line_len, &cursor_idx);
                            pending_escape = 0;
                            continue;
                        }
                        else if (c == KEY_ESC_CURSOR_HOME)
                        {
                            shell_cursor_home(&cursor_idx);
                            pending_escape = 0;
                            continue;
                        }
                        else if (c == KEY_ESC_CURSOR_END)
                        {
                            shell_cursor_end(line_buf, line_len, &cursor_idx);
                            pending_escape = 0;
                            continue;
                        }
                        else if (c == KEY_ESC_DELETE_CHAR)
                        {
                            if (cursor_idx < line_len)
                            {
                                memmove(&line_buf[cursor_idx], &line_buf[cursor_idx + 1], (size_t)(line_len - cursor_idx));
                                line_len--;
                                line_buf[line_len] = '\0';
                                shell_redraw_tail(line_buf, line_len, cursor_idx, cursor_idx);
                            }
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
                        line_buf[line_len] = '\0';
                        goto line_done;
                    }

                    if (c == '\t')
                    {
                        shell_cursor_end(line_buf, line_len, &cursor_idx);
                        int complete_pos = line_len;
                        shell_try_tab_complete(line_buf, &complete_pos);
                        line_len = complete_pos;
                        cursor_idx = line_len;
                        line_buf[line_len] = '\0';
                        continue;
                    }

                    if (c == 0x01) /* Ctrl+A */
                    {
                        shell_cursor_home(&cursor_idx);
                        continue;
                    }

                    if (c == 0x05) /* Ctrl+E */
                    {
                        shell_cursor_end(line_buf, line_len, &cursor_idx);
                        continue;
                    }

                    if (c == 0x15) /* Ctrl+U */
                    {
                        shell_clear_rendered_line(line_buf, line_len, &cursor_idx);
                        line_len = 0;
                        cursor_idx = 0;
                        line_buf[0] = '\0';
                        continue;
                    }

                    if (c == '\b' || c == 127)
                    {
                        if (cursor_idx > 0)
                        {
                            echo_char('\b');
                            cursor_idx--;
                            memmove(&line_buf[cursor_idx], &line_buf[cursor_idx + 1], (size_t)(line_len - cursor_idx));
                            line_len--;
                            line_buf[line_len] = '\0';
                            shell_redraw_tail(line_buf, line_len, cursor_idx, cursor_idx);
                        }
                    }
                    else if (c >= 32 && c < 127 && line_len < INPUT_MAX)
                    {
                        memmove(&line_buf[cursor_idx + 1], &line_buf[cursor_idx], (size_t)(line_len - cursor_idx));
                        line_buf[cursor_idx] = c;
                        line_len++;
                        line_buf[line_len] = '\0';
                        if (cursor_idx == line_len - 1)
                        {
                            echo_char(c);
                        }
                        else
                        {
                            shell_redraw_tail(line_buf, line_len, cursor_idx, cursor_idx + 1);
                        }
                        cursor_idx++;
                    }
                }
            }
        }
line_done:
        if (!is_empty_line(line_buf))
            shell_history_add(line_buf);

        execute_command(line_buf);
        /*
         * After each command line, drop descriptors >2 so a leaked fd does not
         * exhaust the shared table before the next command runs.
         */
        debug_shell_sweep_open_fds();
    }
}
