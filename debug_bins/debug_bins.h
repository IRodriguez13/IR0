/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binaries Interface
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Common header for debug shell commands.
 * Each command is an independent module that only uses syscalls.
 *
 * Usage rule: handlers must behave like userspace binaries.
 * - I/O only through syscalls (SYS_OPEN, SYS_READ, SYS_WRITE, SYS_CLOSE).
 * - Do not call internal kernel functions directly (for example bt_sysfs_*,
 *   hci_*); such binaries would fault if executed in ring 3.
 * - Read/write only through /proc, /sys, /dev using open/read/write/close.
 */

#ifndef _DEBUG_BINS_H
#define _DEBUG_BINS_H

#include <stdint.h>
#include <string.h>
#include <ir0/syscall.h>
#include <ir0/fcntl.h>
#include <ir0/errno.h>
#include <ir0/version.h>

/**
 * debug_strerror - Return errno string (OSDev perror-style, syscall-only compatible)
 * Used by debug bins to print human-readable errors like "Directory not empty"
 */
static inline const char *debug_strerror(int err)
{
    int e = (err < 0) ? -err : err;
    switch (e)
    {
        case EPERM:    return "Operation not permitted";
        case ENOENT:   return "No such file or directory";
        case EACCES:   return "Permission denied";
        case EEXIST:   return "File exists";
        case ENOTDIR:  return "Not a directory";
        case EISDIR:   return "Is a directory";
        case ENOTEMPTY: return "Directory not empty";
        case EINVAL:   return "Invalid argument";
        case ENAMETOOLONG: return "File name too long";
        case ENOSPC:   return "No space left on device";
        case EROFS:    return "Read-only file system";
        case EFAULT:   return "Bad address";
        case ESRCH:    return "No such process";
        case EBADF:    return "Bad file descriptor";
        case ENODEV:   return "No such device";
        case EIO:      return "Input/output error";
        case ELOOP:    return "Too many symbolic links";
        case EXDEV:    return "Cross-device link";
        case ENOSYS:   return "Function not implemented";
        default:       return "Unknown error";
    }
}

/**
 * Helper to write to stdout (fd=1)
 */
static inline void debug_write(const char *str)
{
    if (str)
        syscall(SYS_WRITE, 1, (uint64_t)str, (uint64_t)strlen(str));
}

/**
 * Helper to write to stderr (fd=2)
 */
static inline void debug_write_err(const char *str)
{
    if (str)
        syscall(SYS_WRITE, 2, (uint64_t)str, (uint64_t)strlen(str));
}

/**
 * debug_perror - Print "cmd: path: errstr" to stderr (OSDev perror-style)
 */
static inline void debug_perror(const char *cmd, const char *path, int err)
{
    debug_write_err(cmd);
    debug_write_err(": ");
    if (path && path[0])
    {
        debug_write_err(path);
        debug_write_err(": ");
    }
    debug_write_err(debug_strerror(err));
    debug_write_err("\n");
}

/*
 * Debug commands must not include kernel internals (fs, kernel, ir0/devfs.h,
 * ir0/net.h). They only use syscalls (SYS_OPEN, SYS_READ, SYS_WRITE,
 * SYS_CLOSE, SYS_IOCTL, etc.) or wrappers from ir0/syscall.h.
 *
 * Debug serial: write to /dev/serial via syscalls (kernel forwards to COM1).
 */
static inline void debug_serial_log(const char *cmd, const char *status, const char *reason)
{
    int fd = (int)syscall(SYS_OPEN, (uint64_t)"/dev/serial", O_WRONLY, 0);
    if (fd >= 0)
    {
        char buf[128];
        int n;
        if (reason && reason[0])
            n = snprintf(buf, sizeof(buf), "[DBG] %s: %s %s\n", cmd, status, reason);
        else
            n = snprintf(buf, sizeof(buf), "[DBG] %s: %s\n", cmd, status);
        if (n > 0 && n < (int)sizeof(buf))
            syscall(SYS_WRITE, (uint64_t)fd, (uint64_t)buf, (uint64_t)(size_t)n);
        syscall(SYS_CLOSE, (uint64_t)fd, 0, 0);
    }
}

/*
 * Write a full line to /dev/serial (for example mode/permission diagnostics).
 * The text must include \n if a line break is desired.
 */
static inline void debug_serial_raw(const char *msg)
{
    size_t n;

    if (!msg)
        return;
    n = strlen(msg);
    if (n == 0)
        return;
    {
        int fd = (int)syscall(SYS_OPEN, (uint64_t)"/dev/serial", O_WRONLY, 0);
        if (fd < 0)
            return;
        syscall(SYS_WRITE, (uint64_t)fd, (uint64_t)msg, (uint64_t)n);
        syscall(SYS_CLOSE, (uint64_t)fd, 0, 0);
    }
}

#define debug_serial_ok(cmd) debug_serial_log(cmd, "OK", NULL)
#define debug_serial_fail(cmd, reason) debug_serial_log(cmd, "FAIL", reason)

/*
 * Close fd 3 .. N-1 ignoring errors. DebShell runs commands in one process;
 * if a command leaks an fd, the next one can fail with EMFILE while opening.
 * Keep this aligned with MAX_FDS_PER_PROCESS.
 */
#define DEBUG_SHELL_FD_TABLE_CAP 64

static inline void debug_shell_sweep_open_fds(void)
{
    for (int i = 3; i < DEBUG_SHELL_FD_TABLE_CAP; i++)
        syscall(SYS_CLOSE, (uint64_t)i, 0, 0);
}

/**
 * debug_serial_fail_err - Log failure including errno (e.g. err=17 EEXIST)
 */
static inline void debug_serial_fail_err(const char *cmd, const char *reason, int err)
{
    char err_str[48];
    int e = (err < 0) ? -err : err;
    if (reason && reason[0])
        snprintf(err_str, sizeof(err_str), "%s err=%d", reason, e);
    else
        snprintf(err_str, sizeof(err_str), "err=%d", e);
    debug_serial_log(cmd, "FAIL", err_str);
}

/**
 * Command handler function type
 * @argc: Argument count (includes command name)
 * @argv: Argument array (argv[0] is command name)
 * @return: Exit code (0 = success, !=0 = error)
 */
typedef int (*debug_command_fn)(int argc, char **argv);

/**
 * Command registry structure
 */
struct debug_command {
    const char *name;           /* Command name (e.g. "ls", "cat") */
    debug_command_fn handler;   /* Command handler function */
    const char *usage;          /* Usage string, e.g. "ls [-l] [DIR]" */
    const char *description;    /* Short command description */
    const char *const *flags;   /* Supported flags for tab completion */
};

/**
 * Helper to write full line (with trailing \n)
 */
static inline void debug_writeln(const char *str)
{
    if (str)
    {
        debug_write(str);
        debug_write("\n");
    }
}

/**
 * Helper to write stderr line (with trailing \n)
 */
static inline void debug_writeln_err(const char *str)
{
    if (str)
    {
        debug_write_err(str);
        debug_write_err("\n");
    }
}

/*
 * Format unsigned 64-bit values as decimal without relying on %lu/%llu,
 * because kernel vsnprintf supports only plain integer formats.
 */
static inline void debug_u64_to_dec(uint64_t value, char *out, size_t out_len)
{
    char tmp[32];
    size_t i = 0;
    size_t j = 0;

    if (!out || out_len == 0)
        return;

    if (value == 0)
    {
        if (out_len >= 2)
        {
            out[0] = '0';
            out[1] = '\0';
        }
        else
        {
            out[0] = '\0';
        }
        return;
    }

    while (value != 0 && i < sizeof(tmp))
    {
        tmp[i++] = (char)('0' + (value % 10));
        value /= 10;
    }

    if (i + 1 > out_len)
    {
        i = out_len - 1;
    }

    while (i > 0)
    {
        out[j++] = tmp[--i];
    }
    out[j] = '\0';
}

/**
 * Parse command line string into argc/argv
 * @cmd_line: Full command line
 * @argc_out: Output pointer for argc
 * @argv_out: Output argv array (must have at least 64 entries)
 * @max_args: Maximum argument count (argv_out capacity)
 * @return: 0 on success, -1 on error
 */
/* Forward declaration - implementation in debug_bins_registry.c */
int debug_parse_args(const char *cmd_line, int *argc_out, char **argv_out, int max_args);

/* Find command by name */
struct debug_command *debug_find_command(const char *name);

#endif /* _DEBUG_BINS_H */

