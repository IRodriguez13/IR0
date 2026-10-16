/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: fd_install.h
 * Description: Portable fd install/close/clone (Linux get_unused_fd / fd_install).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

struct vfs_file;

/*
 * Linux fs/file.c get_unused_fd_flags() + fd_install() put the number in
 * current->files. IR0 does the same on current_process->files. The
 * filesystem backend stays behind its own facade; this owner is the
 * generic-core files_struct.
 * https://www.kernel.org/doc/html/latest/filesystems/porting.html
 */
struct ir0_fd_install_ops
{
	int (*install)(int fd, const char *path, int flags);
	int (*check)(int fd);
	int (*close)(int fd);
	int (*clone_files)(int64_t child_pid);
	int (*share_files)(int64_t child_pid);
	struct vfs_file *(*file)(int fd);
};

const struct ir0_fd_install_ops *ir0_fd_install_product(void);
