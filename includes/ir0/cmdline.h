/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: cmdline.h
 * Description: Boot cmdline parser for root=, ir0.loglevel=, ir0.trace= and boot flags.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

/*
 * Apply Kconfig LOG_PROFILE_* default, then override from Multiboot cmdline
 * (flags bit 2 / cmdline pointer). Safe before serial is up — only mutates
 * klog profile/level/trace mask and boot feature flags (e.g. TTY smoke tags).
 */
void ir0_cmdline_apply_log_profile(void);

/*
 * Root block device name: the "root=/dev/<name>" boot parameter (Linux
 * semantics), else CONFIG_ROOT_BLOCK_DEVICE. Never NULL.
 */
const char *ir0_cmdline_root_device(void);

/* Non-zero when Multiboot cmdline contains ir0.tty_smoke=1 (QEMU smokes only). */
int ir0_cmdline_tty_smoke_enabled(void);
