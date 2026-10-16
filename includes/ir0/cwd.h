/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: cwd.h
 * Description: Portable cwd/root owners (Linux current->fs->pwd / getcwd(2)).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * Linux getcwd(2) / chdir(2) / chroot(2) read and write the calling
 * task's fs_struct. IR0 stores the same on current_process. ISA decode
 * stays in the arch; this facade is the generic-core owner.
 * man7.org/linux/man-pages/man2/getcwd.2.html
 */
struct ir0_cwd_ops
{
	int (*get)(char *buf, size_t size);
	int (*set)(const char *path);
	int (*get_root)(char *buf, size_t size);
	int (*set_root)(const char *path);
};

const struct ir0_cwd_ops *ir0_cwd_product(void);
