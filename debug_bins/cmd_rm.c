/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: cmd_rm.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: rm
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Remove file or directory (OSDev-style, syscalls only).
 * - rm FILE: unlink file
 * - rm DIR: rmdir empty dir (POSIX)
 * - rm -d DIR: remove dir and contents (userspace recursive, like POSIX rm -r)
 * - Blocks rm / and rm -d / for safety
 *
 * OSDev approach: kernel only provides unlink + rmdir(empty). Recursive
 * delete is done in userspace via opendir/readdir/unlink/rmdir to avoid
 * kernel hangs on disk I/O.
 */

#include "debug_bins.h"
#include <ir0/fcntl.h>
#include <ir0/stat.h>
#include <string.h>

/* Linux dirent64 (matches kernel getdents) */
struct linux_dirent64 {
    uint64_t d_ino;
    int64_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[];
};

#define MAX_RECURSE_DEPTH 64

static void rm_normalize_path(const char *in, char *out, size_t out_sz)
{
    size_t len = 0;

    if (!in || !out || out_sz == 0)
        return;

    while (in[len] != '\0' && len + 1 < out_sz)
    {
        out[len] = in[len];
        len++;
    }
    out[len] = '\0';

    while (len > 1 && out[len - 1] == '/')
    {
        out[len - 1] = '\0';
        len--;
    }
}

/*
 * Formato tipo ls -l: permisos rwxrwxrwx (sin sticky; suficiente para depurar).
 */
static void rm_fmt_rwx(char rwx[10], uint32_t mode)
{
    rwx[0] = (mode & S_IRUSR) ? 'r' : '-';
    rwx[1] = (mode & S_IWUSR) ? 'w' : '-';
    rwx[2] = (mode & S_IXUSR) ? 'x' : '-';
    rwx[3] = (mode & S_IRGRP) ? 'r' : '-';
    rwx[4] = (mode & S_IWGRP) ? 'w' : '-';
    rwx[5] = (mode & S_IXGRP) ? 'x' : '-';
    rwx[6] = (mode & S_IROTH) ? 'r' : '-';
    rwx[7] = (mode & S_IWOTH) ? 'w' : '-';
    rwx[8] = (mode & S_IXOTH) ? 'x' : '-';
    rwx[9] = '\0';
}

static char rm_type_char(uint32_t mode)
{
    if (S_ISDIR(mode))
        return 'd';
    if (S_ISREG(mode))
        return '-';
    if (S_ISCHR(mode))
        return 'c';
    if (S_ISBLK(mode))
        return 'b';
    if (S_ISLNK(mode))
        return 'l';
    if (S_ISSOCK(mode))
        return 's';
    return '?';
}

static void rm_debug_log_stat_line(const char *phase, const char *path, const stat_t *st)
{
    char rwx[10];
    char line[256];
    uint32_t m = (uint32_t)st->st_mode;

    rm_fmt_rwx(rwx, m);
    snprintf(line, sizeof(line),
             "[DBG] rm: %s path='%s' st_mode=0x%x type=%c perms=%s isdir=%d ino=%u nlink=%u\n",
             phase, path ? path : "?", m, rm_type_char(m), rwx,
             S_ISDIR(m) ? 1 : 0, (unsigned)st->st_ino, (unsigned)st->st_nlink);
    debug_serial_raw(line);
}

static void rm_debug_log_stat_refresh(const char *phase, const char *path)
{
    stat_t st;
    char line[256];

    if (ir0_stat(path, &st) < 0)
    {
        snprintf(line, sizeof(line), "[DBG] rm: %s path='%s' stat_failed\n",
                 phase, path ? path : "?");
        debug_serial_raw(line);
        return;
    }
    rm_debug_log_stat_line(phase, path, &st);
}

/**
 * rm_recursive_impl - Depth-first recursive delete (userspace, OSDev-style)
 * Uses only open/getdents/stat/unlink/rmdir - no kernel recursion.
 */
static int rm_recursive_impl(const char *path, int depth)
{
    if (depth > MAX_RECURSE_DEPTH)
        return -ELOOP;

    stat_t st;
    if (ir0_stat(path, &st) < 0)
        return -ENOENT;

    if (!S_ISDIR(st.st_mode))
        return ir0_unlink(path);

    /* Directory: open, process children first (depth-first), then rmdir */
    int fd = ir0_open(path, O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0)
        return fd;

    char dirent_buf[512];
    int64_t bytes_read;
    int ret = 0;

    while ((bytes_read = syscall(SYS_GETDENTS, fd, (uint64_t)dirent_buf, sizeof(dirent_buf))) > 0)
    {
        size_t offset = 0;
        while (offset < (size_t)bytes_read)
        {
            struct linux_dirent64 *dent = (struct linux_dirent64 *)(dirent_buf + offset);
            if (dent->d_name[0] == '.' &&
                (dent->d_name[1] == '\0' || (dent->d_name[1] == '.' && dent->d_name[2] == '\0')))
            {
                offset += dent->d_reclen;
                continue;
            }

            char full_path[512];
            int len;
            if (path[0] == '/' && path[1] == '\0')
                len = snprintf(full_path, sizeof(full_path), "/%s", dent->d_name);
            else
                len = snprintf(full_path, sizeof(full_path), "%s/%s", path, dent->d_name);
            if (len <= 0 || len >= (int)sizeof(full_path))
            {
                offset += dent->d_reclen;
                continue;
            }

            if (ir0_stat(full_path, &st) < 0)
            {
                offset += dent->d_reclen;
                continue;
            }
            if (S_ISDIR(st.st_mode))
                ret = rm_recursive_impl(full_path, depth + 1);
            else
                ret = ir0_unlink(full_path);
            if (ret < 0)
                break;
            offset += dent->d_reclen;
        }
        if (ret < 0)
            break;
    }

    ir0_close(fd);
    if (ret < 0)
        return ret;
    return ir0_rmdir(path);
}

static int cmd_rm_handler(int argc, char **argv)
{
    if (argc < 2)
    {
        debug_write_err("Usage: rm [-d|-r] <file|dir>\n");
        return 1;
    }

    int force_dir = 0;  /* -d/-r: remove dir recursively if needed */
    const char *path = NULL;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "-r") == 0)
            force_dir = 1;
        else if (argv[i][0] != '-')
            path = argv[i];
    }

    if (!path)
    {
        debug_write_err("Usage: rm [-d|-r] <file|dir>\n");
        debug_serial_fail("rm", "usage");
        return 1;
    }

    char normalized_path[512];
    rm_normalize_path(path, normalized_path, sizeof(normalized_path));
    const char *target = normalized_path[0] ? normalized_path : path;

    if (target[0] == '/' && (target[1] == '\0' || (target[1] == '.' && target[2] == '\0')))
    {
        debug_write_err("rm: cannot remove root directory\n");
        debug_serial_fail("rm", "root");
        return 1;
    }

    int64_t result;
    stat_t st;
    int st_ok = (ir0_stat(target, &st) == 0);

    if (force_dir)
    {
        if (st_ok && !S_ISDIR(st.st_mode))
        {
            result = ir0_unlink(target);
        }
        else
        {
            /*
             * Fast path for empty directories: matches user expectation for
             * rm -d and avoids getdents traversal when not needed.
             */
            result = ir0_rmdir(target);
            if (result == -ENOTEMPTY)
                result = rm_recursive_impl(target, 0);
        }
    }
    else if (st_ok && S_ISDIR(st.st_mode))
        result = ir0_rmdir(target);
    else
    {
        result = ir0_unlink(target);
        if (result < 0)
            result = ir0_rmdir(target);
    }

    if (result < 0)
    {
        debug_perror("rm", target, (int)result);
        if (st_ok)
            rm_debug_log_stat_line("stat@antes", target, &st);
        rm_debug_log_stat_refresh("stat@despues", target);
        if (result == -ENOTDIR && st_ok && S_ISDIR(st.st_mode))
        {
            debug_serial_raw(
                "[DBG] rm: diag stat decia directorio pero rmdir devolvio ENOTDIR (revisar MINIX/VFS)\n");
        }
        if (result == -ENOTEMPTY)
            debug_write_err("Hint: Use 'rm -d DIR' or 'rm -r DIR' for non-empty directory\n");
        debug_serial_fail_err("rm", "remove", (int)(-result));
        return 1;
    }

    {
        char msg[576];
        int n;

        if (force_dir)
            n = snprintf(msg, sizeof(msg), "rm: removed '%s' and contents\n", target);
        else
            n = snprintf(msg, sizeof(msg), "rm: removed '%s'\n", target);
        if (n > 0 && n < (int)sizeof(msg))
            debug_write(msg);
    }
    debug_serial_ok("rm");
    return 0;
}

struct debug_command cmd_rm = {
    .name = "rm",
    .handler = cmd_rm_handler,
    .usage = "rm [-d|-r] FILE",
    .description = "Remove file or directory (-d/-r: recursive dir delete)"
};
