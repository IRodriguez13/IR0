/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: debug_bins.h
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binaries Interface
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Header común para comandos de debug shell.
 * Cada comando es un módulo independiente que solo usa syscalls.
 *
 * Regla de uso: los handlers deben comportarse como userspace.
 * - I/O solo vía syscalls (SYS_OPEN, SYS_READ, SYS_WRITE, SYS_CLOSE).
 * - No llamar a funciones internas del kernel (p.ej. bt_sysfs_*, hci_*);
 *   si el binario se ejecutara en ring 3 daría GPF.
 * - Leer/escribir solo a través de /proc, /sys, /dev con open/read/write/close.
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
        case ENOSYS:   return "Function not implemented";
        default:       return "Unknown error";
    }
}

/**
 * Helper para escribir a stdout (fd=1)
 */
static inline void debug_write(const char *str)
{
    if (str)
        syscall(SYS_WRITE, 1, (uint64_t)str, (uint64_t)strlen(str));
}

/**
 * Helper para escribir a stderr (fd=2)
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
 * Los comandos de debug NO incluyen cabeceras del kernel (fs, kernel, ir0/devfs.h, ir0/net.h).
 * Solo usan syscalls (SYS_OPEN, SYS_READ, SYS_WRITE, SYS_CLOSE, SYS_IOCTL, etc.) o los wrappers
 * ir0_open, ir0_close, ir0_read, ir0_write, ir0_ioctl de ir0/syscall.h.
 *
 * Debug serial: write a /dev/serial vía syscalls (el kernel envía a COM1).
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
 * Escribe una línea completa a /dev/serial (p.ej. diagnóstico con modo/permisos).
 * El texto debe incluir \n si se desea salto de línea.
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
 * Cierra fd 3 .. N-1 ignorando errores. El DebShell corre todo en un solo
 * proceso; si un comando deja un descriptor abierto, el siguiente falla con
 * EMFILE al abrir (p.ej. ls → rm). Debe alinearse con MAX_FDS_PER_PROCESS.
 */
#define DEBUG_SHELL_FD_TABLE_CAP 64

static inline void debug_shell_sweep_open_fds(void)
{
    for (int i = 3; i < DEBUG_SHELL_FD_TABLE_CAP; i++)
        syscall(SYS_CLOSE, (uint64_t)i, 0, 0);
}

/**
 * debug_serial_fail_err - Log fallo con código errno (ej: err=17 EEXIST)
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
 * Tipo de función handler para comandos
 * @argc: Número de argumentos (incluye el nombre del comando)
 * @argv: Array de argumentos (argv[0] es el nombre del comando)
 * @return: Código de salida (0 = éxito, !=0 = error)
 */
typedef int (*debug_command_fn)(int argc, char **argv);

/**
 * Estructura de registro de comando
 */
struct debug_command {
    const char *name;           /* Nombre del comando (ej: "ls", "cat") */
    debug_command_fn handler;   /* Función handler del comando */
    const char *usage;          /* Uso: "ls [-l] [DIR]" */
    const char *description;    /* Descripción corta */
};

/**
 * Helper para escribir línea completa (con \n)
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
 * Helper para escribir error con línea
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
 * Parsear string de argumentos a argc/argv
 * @cmd_line: Línea de comando completa
 * @argc_out: Puntero para retornar argc
 * @argv_out: Array para almacenar argv (debe ser al menos 64 elementos)
 * @max_args: Número máximo de argumentos (tamaño de argv_out)
 * @return: 0 en éxito, -1 en error
 */
/* Forward declaration - implementation in debug_bins_registry.c */
int debug_parse_args(const char *cmd_line, int *argc_out, char **argv_out, int max_args);

/* Función para buscar comando */
struct debug_command *debug_find_command(const char *name);

#endif /* _DEBUG_BINS_H */

