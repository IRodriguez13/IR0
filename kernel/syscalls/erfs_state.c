/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: early_rootfs_state.c
 * Description: Shared state for the split early rootfs backend.
 */

#include "erfs_internal.h"

struct rootfs_slot g_slots[ROOTFS_MAX_OPEN];
const uint8_t *g_blob;
uint64_t g_blob_len;
const struct early_user_services *g_user;
int g_ready;
int g_stdio_open[3];
enum ir0_early_rootfs_node g_stdio_node[3];
uint64_t g_stdio_pos[3];
int g_stdio_flags[3];
int g_stdio_cloexec[3];
enum ir0_early_rootfs_node g_cwd;
enum ir0_early_rootfs_node g_chroot;
const struct ir0_cwd_ops *g_cwd_ops;
int g_cwd_used;
const struct ir0_fd_install_ops *g_fd_ops;
int g_vfs_file_used;
int g_vfs_ops_used;
int g_fd_used;
const struct ir0_product_path_ops *g_product_path;
int g_product_path_used;
uint64_t g_init_vis;
int g_init_from_block;
int g_exec_pending;
int g_exec_owner;
int g_abstract_ok;
char g_exec_args[EXEC_ARGC_MAX][EXEC_ARG_LEN];
char *g_exec_argv[EXEC_ARGC_MAX + 1];
uint64_t g_clock_mono;
uint64_t g_clock_hz;
int64_t g_clock_realtime_off;
struct early_xattr g_xattr[XA_MAX];
struct early_ruleset g_ruleset[RULESET_MAX];
int g_ll_on;
uint64_t g_ll_handled;
int g_ll_n;
struct early_ll_rule g_ll_rules[LL_RULE_MAX];
int g_cpio_ready;
char g_cwd_path[RAMTAB_NAME];
char g_chroot_path[RAMTAB_NAME];
