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
        debug_write_err("Usage: rm [-d] <file|dir>\n");
        return 1;
    }

    int force_dir = 0;  /* -d: remove dir even if non-empty */
    const char *path = NULL;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-d") == 0)
            force_dir = 1;
        else if (argv[i][0] != '-')
            path = argv[i];
    }

    if (!path)
    {
        debug_write_err("Usage: rm [-d] <file|dir>\n");
        debug_serial_fail("rm", "usage");
        return 1;
    }

    if (path[0] == '/' && (path[1] == '\0' || (path[1] == '.' && path[2] == '\0')))
    {
        debug_write_err("rm: cannot remove root directory\n");
        debug_serial_fail("rm", "root");
        return 1;
    }

    int64_t result;
    if (force_dir)
        result = rm_recursive_impl(path, 0);
    else
    {
        result = ir0_unlink(path);
        if (result < 0)
            result = ir0_rmdir(path);
    }

    if (result < 0)
    {
        debug_perror("rm", path, (int)result);
        if (result == -ENOTEMPTY)
            debug_write_err("Hint: Use 'rm -d DIR' to remove non-empty directory\n");
        debug_serial_fail_err("rm", "remove", (int)(-result));
        return 1;
    }

    debug_serial_ok("rm");
    return 0;
}

struct debug_command cmd_rm = {
    .name = "rm",
    .handler = cmd_rm_handler,
    .usage = "rm [-d] FILE",
    .description = "Remove file or directory (-d: dir and contents)"
};
