/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binaries Interface
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Header común para comandos de debug shell.
 * Cada comando es un módulo independiente que solo usa syscalls.
 */

#ifndef _DEBUG_BINS_H
#define _DEBUG_BINS_H

#include <stdint.h>
#include <string.h>
#include <ir0/syscall.h>
#include <ir0/fcntl.h>
#include <ir0/devfs.h>
#include <ir0/version.h>

/* Network types (without functions) - commands should only use syscalls */
#include <ir0/net.h>
/* Remove net_poll from accessible functions - commands must use syscalls only */
#undef net_poll

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
 * Helper para escribir a stdout (fd=1)
 */
static inline void debug_write(const char *str)
{
    if (str)
    {
        syscall(SYS_WRITE, 1, (uint64_t)str, (uint64_t)strlen(str));
    }
}

/**
 * Helper para escribir a stderr (fd=2)
 */
static inline void debug_write_err(const char *str)
{
    if (str)
    {
        syscall(SYS_WRITE, 2, (uint64_t)str, (uint64_t)strlen(str));
    }
}

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

