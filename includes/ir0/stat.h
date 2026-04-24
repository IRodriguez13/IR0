/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: stat.h
 * Description: IR0 kernel source/header file
 */

#ifndef _IR0_STAT_H
#define _IR0_STAT_H

#include <ir0/types.h> /* mode_t, off_t, y tipos de stat */

typedef struct stat stat_t;

/* Structure describing file characteristics */
struct stat {
    dev_t     st_dev;         /* ID of device containing file */
    ino_t     st_ino;         /* Inode number */
    mode_t    st_mode;        /* File type and mode */
    nlink_t   st_nlink;       /* Number of hard links */
    uid_t     st_uid;         /* User ID of owner */
    gid_t     st_gid;         /* Group ID of owner */
    dev_t     st_rdev;        /* Device ID (if special file) */
    off_t     st_size;        /* Total size, in bytes */
    blksize_t st_blksize;     /* Block size for filesystem I/O */
    blkcnt_t  st_blocks;      /* Number of 512B blocks allocated */
    time_t    st_atime;       /* Time of last access */
    time_t    st_mtime;       /* Time of last modification */
    time_t    st_ctime;       /* Time of last status change */
};


/* Test macros for file types */
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)  /* Regular file */
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)  /* Directory */
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)  /* Character device */
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)  /* Block device */
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)  /* FIFO/pipe */
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)  /* Symbolic link */
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK) /* Socket */

#endif /* _IR0_STAT_H */