/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: chmod.c
 * Description: IR0 kernel source/header file
 */

#include "chmod.h"
#include <fs/vfs.h>
#include <ir0/klog.h>
#include <errno.h>
#include <string.h>

/*
 * chmod - Change file access permissions via VFS for the path's mount.
 * @path: Path to file
 * @mode: New permission bits (lower 12 bits applied by the filesystem)
 *
 * Return: 0 on success, negative errno-style code on failure
 */
int chmod(const char *path, mode_t mode)
{
    if (!path)
        return -EINVAL;

    return vfs_chmod(path, mode);
}
