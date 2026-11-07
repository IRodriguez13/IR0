/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: product_pivot.h
 * Description: Initramfs-style tmpfs staging mounts and pivot fold state.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>

/* Mount tmpfs trees used for CPIO pivot (ARM product bind path). */
int ir0_product_pivot_mount_trees(void);

/* Non-zero after at least one staging mount succeeded. */
int ir0_product_pivot_mount_count(void);

/* Set after CPIO members were installed on product VFS. */
void ir0_product_pivot_note_cpio_fold(int installed);

int ir0_product_pivot_cpio_fold_done(void);

extern const char *const ir0_product_pivot_trees[];

int ir0_product_pivot_tree_count(void);
