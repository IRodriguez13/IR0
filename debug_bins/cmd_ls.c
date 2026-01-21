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
        debug_write_err("ls: cannot access '");
        debug_write_err(path);
        debug_write_err("': ");
        
        /* Print error message based on error code */
        int err = -fd;
        if (err == ENOENT)
            debug_write_err("No such file or directory");
        else if (err == EACCES)
            debug_write_err("Permission denied");
        else if (err == ENOTDIR)
            debug_write_err("Not a directory");
        else if (err == EFAULT)
            debug_write_err("Invalid address");
        else if (err == ESRCH)
            debug_write_err("No such process");
        else
        {
            /* Print numeric error code */
            char err_buf[32];
            snprintf(err_buf, sizeof(err_buf), "Error %d", err);
            debug_write_err(err_buf);
        }
        debug_write_err("\n");
        return 1;
    }
    
    /* Read directory entries using POSIX getdents */
    char dirent_buf[4096];
    int64_t bytes_read;
    
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
                int len = snprintf(full_path, sizeof(full_path), "%s/%s", path, dent->d_name);
                if (len > 0 && len < (int)sizeof(full_path))
                {
                    stat_t st;
                    int64_t stat_result = syscall(SYS_STAT, (uint64_t)full_path, (uint64_t)&st, 0);
                    
                    if (stat_result >= 0)
                    {
                        /* Format: mode links uid gid size name */
                        char mode_str[11] = "----------";
                        if (S_ISDIR(st.st_mode)) mode_str[0] = 'd';
                        if (S_ISREG(st.st_mode)) mode_str[0] = '-';
                        if (S_ISCHR(st.st_mode)) mode_str[0] = 'c';
                        if (S_ISBLK(st.st_mode)) mode_str[0] = 'b';
                        if (S_ISLNK(st.st_mode)) mode_str[0] = 'l';
                        if (st.st_mode & S_IRUSR) mode_str[1] = 'r';
                        if (st.st_mode & S_IWUSR) mode_str[2] = 'w';
                        if (st.st_mode & S_IXUSR) mode_str[3] = 'x';
                        if (st.st_mode & S_IRGRP) mode_str[4] = 'r';
                        if (st.st_mode & S_IWGRP) mode_str[5] = 'w';
                        if (st.st_mode & S_IXGRP) mode_str[6] = 'x';
                        if (st.st_mode & S_IROTH) mode_str[7] = 'r';
                        if (st.st_mode & S_IWOTH) mode_str[8] = 'w';
                        if (st.st_mode & S_IXOTH) mode_str[9] = 'x';
                        
                        char line[256];
                        int line_len = snprintf(line, sizeof(line), "%s %u %u %u %llu %s\n",
                                              mode_str, (unsigned)st.st_nlink, 
                                              (unsigned)st.st_uid, (unsigned)st.st_gid,
                                              (unsigned long long)st.st_size,
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
            }
            
            offset += dent->d_reclen;
        }
    }
    
    if (!detailed && bytes_read > 0)
    {
        debug_write("\n");
    }
    
    /* Close directory */
    syscall(SYS_CLOSE, fd, 0, 0);
    
    if (bytes_read < 0)
    {
        debug_write_err("ls: error reading directory\n");
        return 1;
    }
    
    return 0;
}

struct debug_command cmd_ls = {
    .name = "ls",
    .handler = cmd_ls_handler,
    .usage = "ls [-l] [-a] [DIR]",
    .description = "List directory contents"
};
