/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: bpivot.h
 * Description: Boot pivot — tmpfs FHS trees, staging, and ramtab commit.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <ir0/bpath.h>

#define IR0_BPIVOT_NEW_ROOT "/bpivot/new"
#define IR0_BPIVOT_PUT_OLD   "/bpivot/new/old"

int ir0_bpivot_mount_trees(void);
int ir0_bpivot_mount_count(void);
void ir0_bpivot_note_cpio_fold(int installed);
int ir0_bpivot_cpio_fold_done(void);
extern const char *const ir0_bpivot_trees[];
int ir0_bpivot_tree_count(void);

int ir0_bpivot_prepare_staging(void);
int ir0_bpivot_commit(void);
int ir0_bpivot_committed(void);
int ir0_bpivot_root_switched(void);
int ir0_bpivot_stage_path(const char *path, char *out, size_t out_size);

const struct ir0_bpath_ops *ir0_bpath_vfs_product_ops(void);

/* ISA-neutral boot sequence: mount trees, bpath bind, optional CPIO fold, commit. */
int ir0_bpivot_product_boot(const struct ir0_bpath_ops *ops);
