/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: cmd_ls.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: ls
 * Copyright (C) 2026 Iván Rodriguez
 *
 * List directory contents command using POSIX syscalls only
 */

#include "debug_bins.h"
#include <ir0/fcntl.h>
#include <ir0/stat.h>
#include <ir0/errno.h>
#include <string.h>

/* Linux dirent64 structure (matches kernel) */
struct linux_dirent64 {
    uint64_t d_ino;
    int64_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[];
};

/* Directory entry types */
#define DT_UNKNOWN 0
#define DT_DIR 4
#define DT_REG 8
#define DT_CHR 2
#define DT_BLK 6
#define DT_LNK 10

static int cmd_ls_handler(int argc, char **argv)
{
    const char *path = NULL;
    int detailed = 0;
    int show_all = 0;
    char cwd[256];
    
    /* Parse arguments: ls [-l] [-a] [DIR] */
    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-')
        {
            for (int j = 1; argv[i][j] != '\0'; j++)
            {
                if (argv[i][j] == 'l')
                    detailed = 1;
                else if (argv[i][j] == 'a')
                    show_all = 1;
            }
        }
        else
        {
            path = argv[i];
        }
    }
    
    /* If no path specified, use current directory */
    if (!path)
    {
        int64_t result = syscall(SYS_GETCWD, (uint64_t)cwd, sizeof(cwd), 0);
        if (result >= 0)
        {
            path = cwd;
        }
        else
        {
            path = "/";
        }
    }
    
    /* Open directory using POSIX syscall */
    int fd = syscall(SYS_OPEN, (uint64_t)path, O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0)
    {
        debug_perror("ls", path, (int)fd);
        debug_serial_fail_err("ls", "open", (int)(-fd));
        return 1;
    }
    
    /*
     * Buffer pequeño: el init del DebShell usa pila de pocos KiB; 4096 bytes
     * aquí desbordaba la pila y podía corromper el heap (p.ej. fd_table → EMFILE).
     */
    char dirent_buf[1024];
    int64_t bytes_read;
    int wrote_simple_entries = 0;
    
    while ((bytes_read = syscall(SYS_GETDENTS, fd, (uint64_t)dirent_buf, sizeof(dirent_buf))) > 0)
    {
        size_t offset = 0;
        
        while (offset < (size_t)bytes_read)
        {
            struct linux_dirent64 *dent = (struct linux_dirent64 *)(dirent_buf + offset);
            
            /* Skip . and .. unless -a */
            if (!show_all && (dent->d_name[0] == '.'))
            {
                offset += dent->d_reclen;
                continue;
            }
            
            if (detailed)
            {
                /* Get stat for detailed info */
                char full_path[512];
                int len;
                if (path[0] == '/' && path[1] == '\0')
                    len = snprintf(full_path, sizeof(full_path), "/%s", dent->d_name);
                else
                    len = snprintf(full_path, sizeof(full_path), "%s/%s", path, dent->d_name);
                if (len > 0 && len < (int)sizeof(full_path))
                {
                    stat_t st;
                    int64_t stat_result = syscall(SYS_STAT, (uint64_t)full_path, (uint64_t)&st, 0);
                    
                    if (stat_result >= 0)
                    {
                        /* Format: mode links uid gid size name (Unix rwxrwxrwx) */
                        char mode_str[12];
                        mode_str[0] = S_ISDIR(st.st_mode) ? 'd' :
                                      S_ISREG(st.st_mode) ? '-' :
                                      S_ISCHR(st.st_mode) ? 'c' :
                                      S_ISBLK(st.st_mode) ? 'b' :
                                      S_ISLNK(st.st_mode) ? 'l' :
                                      S_ISSOCK(st.st_mode) ? 's' : '-';
                        mode_str[1]  = (st.st_mode & S_IRUSR) ? 'r' : '-';
                        mode_str[2]  = (st.st_mode & S_IWUSR) ? 'w' : '-';
                        mode_str[3]  = (st.st_mode & S_IXUSR) ? 'x' : '-';
                        mode_str[4]  = (st.st_mode & S_IRGRP) ? 'r' : '-';
                        mode_str[5]  = (st.st_mode & S_IWGRP) ? 'w' : '-';
                        mode_str[6]  = (st.st_mode & S_IXGRP) ? 'x' : '-';
                        mode_str[7]  = (st.st_mode & S_IROTH) ? 'r' : '-';
                        mode_str[8]  = (st.st_mode & S_IWOTH) ? 'w' : '-';
                        mode_str[9]  = (st.st_mode & S_IXOTH) ? 'x' : '-';
                        mode_str[10] = '\0';
                        
                        char size_str[32];
                        char line[256];
                        debug_u64_to_dec((uint64_t)st.st_size, size_str, sizeof(size_str));
                        int line_len = snprintf(line, sizeof(line), "%s %u %u %u %s %s\n",
                                              mode_str, (unsigned)st.st_nlink, 
                                              (unsigned)st.st_uid, (unsigned)st.st_gid,
                                              size_str,
                                              dent->d_name);
                        if (line_len > 0 && line_len < (int)sizeof(line))
                        {
                            debug_write(line);
                        }
                    }
                    else
                    {
                        /* Fallback: just print name */
                        debug_write(dent->d_name);
                        debug_write("\n");
                    }
                }
            }
            else
            {
                /* Simple listing */
                debug_write(dent->d_name);
                debug_write("  ");
                wrote_simple_entries = 1;
            }
            
            offset += dent->d_reclen;
        }
    }
    
    if (!detailed && wrote_simple_entries)
    {
        debug_write("\n");
    }
    
    /* Close directory */
    syscall(SYS_CLOSE, fd, 0, 0);
    
    if (bytes_read < 0)
    {
        debug_write_err("ls: error reading directory\n");
        debug_serial_fail("ls", "readdir");
        return 1;
    }
    
    debug_serial_ok("ls");
    return 0;
}

struct debug_command cmd_ls = {
    .name = "ls",
    .handler = cmd_ls_handler,
    .usage = "ls [-l] [-a] [DIR]",
    .description = "List directory contents"
};
