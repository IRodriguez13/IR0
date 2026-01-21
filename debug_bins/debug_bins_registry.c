/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binaries Registry
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Registry de todos los comandos disponibles en debug_bins/
 */

#include "debug_bins.h"

/* Declaraciones externas de comandos Unix reales */
extern struct debug_command cmd_ls;
extern struct debug_command cmd_cd;
extern struct debug_command cmd_pwd;
extern struct debug_command cmd_cat;
extern struct debug_command cmd_mkdir;
extern struct debug_command cmd_rm;
extern struct debug_command cmd_rmdir;
extern struct debug_command cmd_touch;
extern struct debug_command cmd_cp;
extern struct debug_command cmd_mv;
extern struct debug_command cmd_ln;
extern struct debug_command cmd_echo;
extern struct debug_command cmd_exec;
extern struct debug_command cmd_sed;
extern struct debug_command cmd_mount;
extern struct debug_command cmd_chmod;
extern struct debug_command cmd_chown;
extern struct debug_command cmd_ps;
extern struct debug_command cmd_df;
extern struct debug_command cmd_dmesg;
extern struct debug_command cmd_ping;
extern struct debug_command cmd_uname;
extern struct debug_command cmd_lsblk;
extern struct debug_command cmd_lsdrv;
extern struct debug_command cmd_mkswap;
extern struct debug_command cmd_swapon;
extern struct debug_command cmd_swapoff;

/* Tabla de comandos disponibles (solo comandos Unix reales) */
struct debug_command *debug_commands[] = {
    &cmd_ls,
    &cmd_cd,
    &cmd_pwd,
    &cmd_cat,
    &cmd_mkdir,
    &cmd_rm,
    &cmd_rmdir,
    &cmd_touch,
    &cmd_cp,
    &cmd_mv,
    &cmd_ln,
    &cmd_echo,
    &cmd_exec,
    &cmd_sed,
    &cmd_mount,
    &cmd_chmod,
    &cmd_chown,
    &cmd_ps,
    &cmd_df,
    &cmd_dmesg,
    &cmd_ping,
    &cmd_uname,
    &cmd_lsblk,
    &cmd_lsdrv,
    NULL  /* Terminador */
};

/**
 * debug_find_command - Buscar comando por nombre
 * @name: Nombre del comando
 * @return: Puntero al comando o NULL si no se encuentra
 */
struct debug_command *debug_find_command(const char *name)
{
    if (!name)
        return NULL;
    
    for (int i = 0; debug_commands[i] != NULL; i++)
    {
        if (strcmp(debug_commands[i]->name, name) == 0)
        {
            return debug_commands[i];
        }
    }
    
    return NULL;
}

/**
 * Parsear string de argumentos a argc/argv
 */
int debug_parse_args(const char *cmd_line, int *argc_out, char **argv_out, int max_args)
{
    if (!cmd_line || !argc_out || !argv_out || max_args < 1)
        return -1;
    
    int argc = 0;
    const char *p = cmd_line;
    
    /* Skip leading whitespace */
    while (*p == ' ' || *p == '\t')
        p++;
    
    if (*p == '\0')
    {
        *argc_out = 0;
        return 0;
    }
    
    /* Parse tokens */
    static char token_buf[64][256];
    static int token_idx = 0;
    
    while (*p && argc < max_args - 1)
    {
        /* Skip whitespace */
        while (*p == ' ' || *p == '\t')
            p++;
        
        if (*p == '\0')
            break;
        
        /* Find start of token */
        const char *start = p;
        
        /* Find end of token (whitespace or end of string) */
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
            p++;
        
        /* Store token */
        size_t len = (size_t)(p - start);
        if (len > 0)
        {
            if (len >= 256)
                len = 255;
            
            char *token = token_buf[token_idx % 64];
            for (size_t i = 0; i < len; i++)
                token[i] = start[i];
            token[len] = '\0';
            
            argv_out[argc++] = token;
            token_idx++;
        }
        
        if (*p == '\0')
            break;
    }
    
    argv_out[argc] = NULL;  /* NULL terminator */
    *argc_out = argc;
    
    return 0;
}

