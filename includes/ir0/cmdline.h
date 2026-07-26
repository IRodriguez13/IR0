/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: cmdline.h
 * Description: Multiboot cmdline parser for ir0.loglevel= / ir0.trace=.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

/*
 * Apply Kconfig LOG_PROFILE_* default, then override from Multiboot cmdline
 * (flags bit 2 / cmdline pointer). Safe before serial is up — only mutates
 * klog profile/level/trace mask.
 */
void ir0_cmdline_apply_log_profile(void);
