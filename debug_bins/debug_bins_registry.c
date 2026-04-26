/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binaries Registry
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Registry for all commands available in debug_bins/
 */

#include "debug_bins.h"
#include <config.h>

/* External declarations for debug commands */
#if CONFIG_DEBUG_BINS_GROUP_CORE
extern struct debug_command cmd_ls;
extern struct debug_command cmd_cd;
extern struct debug_command cmd_pwd;
extern struct debug_command cmd_cat;
extern struct debug_command cmd_echo;
extern struct debug_command cmd_exec;
extern struct debug_command cmd_cmp;
extern struct debug_command cmd_which;
extern struct debug_command cmd_true;
extern struct debug_command cmd_false;
extern struct debug_command cmd_sleep;
#endif
#if CONFIG_DEBUG_BINS_GROUP_FS
extern struct debug_command cmd_mkdir;
extern struct debug_command cmd_rm;
extern struct debug_command cmd_rmdir;
extern struct debug_command cmd_touch;
extern struct debug_command cmd_cp;
extern struct debug_command cmd_mv;
extern struct debug_command cmd_ln;
extern struct debug_command cmd_mount;
extern struct debug_command cmd_umount;
extern struct debug_command cmd_chmod;
extern struct debug_command cmd_chown;
extern struct debug_command cmd_basename;
extern struct debug_command cmd_dirname;
#endif
#if CONFIG_DEBUG_BINS_GROUP_DIAG
extern struct debug_command cmd_ps;
extern struct debug_command cmd_df;
extern struct debug_command cmd_dmesg;
extern struct debug_command cmd_lsmod;
extern struct debug_command cmd_hostname;
extern struct debug_command cmd_uname;
extern struct debug_command cmd_lsblk;
extern struct debug_command cmd_lsdrv;
extern struct debug_command cmd_free;
extern struct debug_command cmd_uptime;
extern struct debug_command cmd_date;
extern struct debug_command cmd_keymap;
extern struct debug_command cmd_lshw;
extern struct debug_command cmd_stat;
#endif
#if CONFIG_DEBUG_BINS_GROUP_NET
extern struct debug_command cmd_ping;
extern struct debug_command cmd_ndev;
extern struct debug_command cmd_route;
extern struct debug_command cmd_ifconfig;
extern struct debug_command cmd_netstat;
#endif
#if CONFIG_DEBUG_BINS_GROUP_BT
extern struct debug_command cmd_lsblue;
extern struct debug_command cmd_bluestart;
extern struct debug_command cmd_blue;
#endif
#if CONFIG_DEBUG_BINS_GROUP_IDENTITY
extern struct debug_command cmd_sudo;
extern struct debug_command cmd_id;
extern struct debug_command cmd_whoami;
#endif
#if CONFIG_DEBUG_BINS_GROUP_TEXT
extern struct debug_command cmd_sed;
extern struct debug_command cmd_cut;
extern struct debug_command cmd_tr;
extern struct debug_command cmd_wc;
extern struct debug_command cmd_head;
extern struct debug_command cmd_tail;
#endif
#ifdef IR0_KERNEL_TESTS
extern struct debug_command cmd_ktest;
#endif

/* Table of available commands */
struct debug_command *debug_commands[] = {
#if CONFIG_DEBUG_BINS_GROUP_CORE
    &cmd_ls,
    &cmd_cd,
    &cmd_pwd,
    &cmd_cat,
    &cmd_echo,
    &cmd_exec,
    &cmd_cmp,
    &cmd_which,
    &cmd_true,
    &cmd_false,
    &cmd_sleep,
#endif
#if CONFIG_DEBUG_BINS_GROUP_FS
    &cmd_mkdir,
    &cmd_rm,
    &cmd_rmdir,
    &cmd_touch,
    &cmd_cp,
    &cmd_mv,
    &cmd_ln,
    &cmd_mount,
    &cmd_umount,
    &cmd_chmod,
    &cmd_chown,
    &cmd_basename,
    &cmd_dirname,
#endif
#if CONFIG_DEBUG_BINS_GROUP_DIAG
    &cmd_ps,
    &cmd_df,
    &cmd_dmesg,
    &cmd_lsmod,
    &cmd_hostname,
    &cmd_uname,
    &cmd_lsblk,
    &cmd_lsdrv,
    &cmd_free,
    &cmd_uptime,
    &cmd_date,
    &cmd_keymap,
    &cmd_lshw,
#endif
#if CONFIG_DEBUG_BINS_GROUP_NET
    &cmd_ping,
    &cmd_ndev,
    &cmd_route,
    &cmd_ifconfig,
    &cmd_netstat,
#endif
#if CONFIG_DEBUG_BINS_GROUP_BT
    &cmd_lsblue,
    &cmd_bluestart,
    &cmd_blue,
#endif
#if CONFIG_DEBUG_BINS_GROUP_IDENTITY
    &cmd_sudo,
    &cmd_id,
    &cmd_whoami,
#endif
#if CONFIG_DEBUG_BINS_GROUP_TEXT
    &cmd_sed,
    &cmd_cut,
    &cmd_tr,
    &cmd_wc,
    &cmd_head,
    &cmd_tail,
#endif
#if CONFIG_DEBUG_BINS_GROUP_DIAG
    &cmd_stat,
#endif
#ifdef IR0_KERNEL_TESTS
    &cmd_ktest,
#endif
    NULL  /* Terminator */
};

/**
 * debug_find_command - Find command by name
 * @name: Command name
 * @return: Command pointer or NULL if not found
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
 * Parse argument string into argc/argv
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

static int debug_name_in_list(const char *name, const char *const *list)
{
    if (!name || !list)
        return 0;

    for (int i = 0; list[i] != NULL; i++)
    {
        if (strcmp(name, list[i]) == 0)
            return 1;
    }
    return 0;
}

const char *debug_command_section(const char *name)
{
    static const char *const core_cmds[] = {
        "ls", "cd", "pwd", "cat", "echo", "exec", "cmp", "which", "true", "false", "sleep", NULL
    };
    static const char *const fs_cmds[] = {
        "mkdir", "rm", "rmdir", "touch", "cp", "mv", "ln", "mount", "umount", "chmod", "chown", "basename", "dirname", NULL
    };
    static const char *const diag_cmds[] = {
        "ps", "df", "dmesg", "lsmod", "hostname", "uname", "lsblk", "lsdrv", "free",
        "uptime", "date", "keymap", "lshw", "stat", NULL
    };
    static const char *const net_cmds[] = {
        "ping", "ndev", "route", "ifconfig", "netstat", NULL
    };
    static const char *const bt_cmds[] = {
        "lsblue", "bluestart", "blue", NULL
    };
    static const char *const identity_cmds[] = {
        "sudo", "id", "whoami", NULL
    };
    static const char *const text_cmds[] = {
        "sed", "cut", "tr", "wc", "head", "tail", NULL
    };

    if (!name || !name[0])
        return NULL;

    if (debug_name_in_list(name, core_cmds))
        return "core";
    if (debug_name_in_list(name, fs_cmds))
        return "fs";
    if (debug_name_in_list(name, diag_cmds))
        return "diag";
    if (debug_name_in_list(name, net_cmds))
        return "net";
    if (debug_name_in_list(name, bt_cmds))
        return "bt";
    if (debug_name_in_list(name, identity_cmds))
        return "identity";
    if (debug_name_in_list(name, text_cmds))
        return "text";
#ifdef IR0_KERNEL_TESTS
    if (strcmp(name, "ktest") == 0)
        return "diag";
#endif
    return NULL;
}

int debug_is_valid_section(const char *section)
{
    if (!section || !section[0])
        return 0;

    return strcmp(section, "core") == 0 ||
           strcmp(section, "fs") == 0 ||
           strcmp(section, "diag") == 0 ||
           strcmp(section, "net") == 0 ||
           strcmp(section, "bt") == 0 ||
           strcmp(section, "identity") == 0 ||
           strcmp(section, "text") == 0 ||
           strcmp(section, "shell") == 0;
}

