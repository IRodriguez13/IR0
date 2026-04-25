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

static const char *const ls_flags[] = {
    "-l",
    "-a",
    "-1",
    "-h",
    "-R",
    "-F",
    NULL
};

struct ls_options
{
    int detailed;
    int show_all;
    int one_per_line;
    int human_readable;
    int recursive;
    int classify;
};

static int ls_join_path(const char *dir, const char *name, char *out, size_t out_sz)
{
    int n;

    if (!dir || !name || !out || out_sz == 0)
        return -EINVAL;
    if (dir[0] == '/' && dir[1] == '\0')
        n = snprintf(out, out_sz, "/%s", name);
    else
        n = snprintf(out, out_sz, "%s/%s", dir, name);
    if (n <= 0 || n >= (int)out_sz)
        return -ENAMETOOLONG;
    return 0;
}

static void ls_format_size(uint64_t value, int human_readable, char *out, size_t out_sz)
{
    if (!human_readable)
    {
        debug_u64_to_dec(value, out, out_sz);
        return;
    }

    const char *units[] = {"B", "K", "M", "G", "T"};
    uint64_t whole = value;
    uint64_t frac = 0;
    int u = 0;

    while (whole >= 1024 && u < 4)
    {
        frac = ((whole % 1024) * 10) / 1024;
        whole /= 1024;
        u++;
    }

    if (u == 0)
    {
        char whole_str[32];
        debug_u64_to_dec(whole, whole_str, sizeof(whole_str));
        snprintf(out, out_sz, "%s%s", whole_str, units[u]);
    }
    else
    {
        char whole_str[32];
        char frac_str[8];
        debug_u64_to_dec(whole, whole_str, sizeof(whole_str));
        debug_u64_to_dec(frac, frac_str, sizeof(frac_str));
        snprintf(out, out_sz, "%s.%s%s", whole_str, frac_str, units[u]);
    }
}

static char ls_entry_type_suffix(const stat_t *st)
{
    if (S_ISDIR(st->st_mode))
        return '/';
    if (S_ISLNK(st->st_mode))
        return '@';
    if (st->st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))
        return '*';
    return '\0';
}

static void ls_mode_string(const stat_t *st, char mode_str[11])
{
    mode_str[0] = S_ISDIR(st->st_mode) ? 'd' :
                  S_ISREG(st->st_mode) ? '-' :
                  S_ISCHR(st->st_mode) ? 'c' :
                  S_ISBLK(st->st_mode) ? 'b' :
                  S_ISLNK(st->st_mode) ? 'l' :
                  S_ISSOCK(st->st_mode) ? 's' : '?';
    mode_str[1] = (st->st_mode & S_IRUSR) ? 'r' : '-';
    mode_str[2] = (st->st_mode & S_IWUSR) ? 'w' : '-';
    mode_str[3] = (st->st_mode & S_IXUSR) ? 'x' : '-';
    mode_str[4] = (st->st_mode & S_IRGRP) ? 'r' : '-';
    mode_str[5] = (st->st_mode & S_IWGRP) ? 'w' : '-';
    mode_str[6] = (st->st_mode & S_IXGRP) ? 'x' : '-';
    mode_str[7] = (st->st_mode & S_IROTH) ? 'r' : '-';
    mode_str[8] = (st->st_mode & S_IWOTH) ? 'w' : '-';
    mode_str[9] = (st->st_mode & S_IXOTH) ? 'x' : '-';
    mode_str[10] = '\0';
}

static int ls_print_entry(const char *dir, const char *name, const struct ls_options *opts)
{
    char full_path[512];
    stat_t st;
    int suffix = '\0';

    if (ls_join_path(dir, name, full_path, sizeof(full_path)) != 0)
        return 0;

    if (syscall(SYS_STAT, (uint64_t)full_path, (uint64_t)&st, 0) < 0)
    {
        debug_write(name);
        if (opts->one_per_line || opts->detailed)
            debug_write("\n");
        else
            debug_write("  ");
        return 0;
    }

    if (opts->classify)
        suffix = ls_entry_type_suffix(&st);

    if (opts->detailed)
    {
        char mode_str[11];
        char size_str[32];
        char line[320];
        ls_mode_string(&st, mode_str);
        ls_format_size((uint64_t)st.st_size, opts->human_readable, size_str, sizeof(size_str));
        if (suffix != '\0')
        {
            snprintf(line, sizeof(line), "%s %u %u %u %s %s%c\n",
                     mode_str, (unsigned)st.st_nlink, (unsigned)st.st_uid,
                     (unsigned)st.st_gid, size_str, name, suffix);
        }
        else
        {
            snprintf(line, sizeof(line), "%s %u %u %u %s %s\n",
                     mode_str, (unsigned)st.st_nlink, (unsigned)st.st_uid,
                     (unsigned)st.st_gid, size_str, name);
        }
        debug_write(line);
    }
    else
    {
        debug_write(name);
        if (suffix != '\0')
        {
            char s[2] = {(char)suffix, '\0'};
            debug_write(s);
        }
        if (opts->one_per_line)
            debug_write("\n");
        else
            debug_write("  ");
    }

    return S_ISDIR(st.st_mode) ? 1 : 0;
}

static int ls_list_directory(const char *path, const struct ls_options *opts, int print_header, int depth)
{
    if (depth > 32)
        return -ELOOP;

    int fd = (int)syscall(SYS_OPEN, (uint64_t)path, O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0)
        return fd;

    if (print_header)
    {
        debug_write(path);
        debug_write(":\n");
    }

    char dirent_buf[1024];
    int64_t bytes_read;
    int wrote_simple_entries = 0;
    int nested_error = 0;

    while ((bytes_read = syscall(SYS_GETDENTS, fd, (uint64_t)dirent_buf, sizeof(dirent_buf))) > 0)
    {
        size_t offset = 0;
        while (offset < (size_t)bytes_read)
        {
            struct linux_dirent64 *dent = (struct linux_dirent64 *)(dirent_buf + offset);
            size_t reclen = (size_t)dent->d_reclen;

            if (reclen < sizeof(struct linux_dirent64) || offset + reclen > (size_t)bytes_read)
            {
                syscall(SYS_CLOSE, fd, 0, 0);
                return -EIO;
            }

            int is_dot = (dent->d_name[0] == '.' && dent->d_name[1] == '\0');
            int is_dotdot = (dent->d_name[0] == '.' && dent->d_name[1] == '.' && dent->d_name[2] == '\0');

            if (!opts->show_all && dent->d_name[0] == '.')
            {
                offset += reclen;
                continue;
            }

            if (ls_print_entry(path, dent->d_name, opts) >= 0 &&
                !opts->detailed && !opts->one_per_line)
            {
                wrote_simple_entries = 1;
            }

            if (opts->recursive && !is_dot && !is_dotdot)
            {
                char full_path[512];
                stat_t st;
                if (ls_join_path(path, dent->d_name, full_path, sizeof(full_path)) == 0 &&
                    syscall(SYS_STAT, (uint64_t)full_path, (uint64_t)&st, 0) == 0 &&
                    S_ISDIR(st.st_mode))
                {
                    debug_write("\n");
                    int rc = ls_list_directory(full_path, opts, 1, depth + 1);
                    if (rc < 0 && nested_error == 0)
                        nested_error = rc;
                }
            }

            offset += reclen;
        }
    }

    syscall(SYS_CLOSE, fd, 0, 0);

    if (bytes_read < 0)
        return (int)bytes_read;
    if (nested_error < 0)
        return nested_error;

    if (!opts->detailed && !opts->one_per_line && wrote_simple_entries)
        debug_write("\n");

    return 0;
}

static int ls_parse_args(int argc, char **argv, struct ls_options *opts, const char **paths, int *path_count)
{
    *path_count = 0;
    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-')
        {
            if (argv[i][1] == '\0')
            {
                debug_write_err("ls: invalid option '-'\n");
                return -EINVAL;
            }
            for (int j = 1; argv[i][j] != '\0'; j++)
            {
                switch (argv[i][j])
                {
                    case 'l':
                        opts->detailed = 1;
                        break;
                    case 'a':
                        opts->show_all = 1;
                        break;
                    case '1':
                        opts->one_per_line = 1;
                        break;
                    case 'h':
                        opts->human_readable = 1;
                        break;
                    case 'R':
                        opts->recursive = 1;
                        break;
                    case 'F':
                        opts->classify = 1;
                        break;
                    default:
                        debug_write_err("ls: unknown option\n");
                        return -EINVAL;
                }
            }
        }
        else
        {
            if (*path_count < 64)
            {
                paths[*path_count] = argv[i];
                (*path_count)++;
            }
        }
    }
    return 0;
}

static int cmd_ls_handler(int argc, char **argv)
{
    struct ls_options opts = {0};
    const char *paths[64];
    int path_count = 0;
    char cwd[256];

    if (ls_parse_args(argc, argv, &opts, paths, &path_count) != 0)
    {
        debug_serial_fail("ls", "usage");
        return 1;
    }

    if (path_count == 0)
    {
        int64_t result = syscall(SYS_GETCWD, (uint64_t)cwd, sizeof(cwd), 0);
        if (result >= 0)
            paths[path_count++] = cwd;
        else
            paths[path_count++] = "/";
    }

    int had_error = 0;
    for (int i = 0; i < path_count; i++)
    {
        const char *path = paths[i];
        stat_t st;
        int show_header = (path_count > 1) || opts.recursive;

        int64_t stat_ret = syscall(SYS_STAT, (uint64_t)path, (uint64_t)&st, 0);
        if (stat_ret < 0)
        {
            debug_perror("ls", path, (int)stat_ret);
            debug_serial_fail_err("ls", "stat", (int)(-stat_ret));
            had_error = 1;
            continue;
        }

        if (!S_ISDIR(st.st_mode))
        {
            int suffix = opts.classify ? ls_entry_type_suffix(&st) : '\0';
            if (show_header)
            {
                debug_write(path);
                debug_write(":\n");
            }
            if (opts.detailed)
            {
                char mode_str[11];
                char size_str[32];
                char line[320];
                ls_mode_string(&st, mode_str);
                ls_format_size((uint64_t)st.st_size, opts.human_readable, size_str, sizeof(size_str));
                if (suffix != '\0')
                {
                    snprintf(line, sizeof(line), "%s %u %u %u %s %s%c\n",
                             mode_str, (unsigned)st.st_nlink, (unsigned)st.st_uid,
                             (unsigned)st.st_gid, size_str, path, suffix);
                }
                else
                {
                    snprintf(line, sizeof(line), "%s %u %u %u %s %s\n",
                             mode_str, (unsigned)st.st_nlink, (unsigned)st.st_uid,
                             (unsigned)st.st_gid, size_str, path);
                }
                debug_write(line);
            }
            else
            {
                debug_write(path);
                if (suffix != '\0')
                {
                    char s[2] = {(char)suffix, '\0'};
                    debug_write(s);
                }
                debug_write("\n");
            }
            continue;
        }

        int ret = ls_list_directory(path, &opts, show_header, 0);
        if (ret < 0)
        {
            debug_perror("ls", path, ret);
            debug_serial_fail_err("ls", "readdir", (int)(-ret));
            had_error = 1;
        }

        if (i + 1 < path_count)
            debug_write("\n");
    }

    if (had_error)
        return 1;

    debug_serial_ok("ls");
    return 0;
}
struct debug_command cmd_ls = {
    .name = "ls",
    .handler = cmd_ls_handler,
    .usage = "ls [-la1hRF] [PATH...]",
    .description = "List directory contents",
    .flags = ls_flags
};
