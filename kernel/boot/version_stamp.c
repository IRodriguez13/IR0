/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: version_stamp.c
 * Description: Linker-visible release string for kmang identity checks.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <config.h>

/*
 * Prefix lets scripts locate the release in .rodata without parsing the full
 * ELF symbol table. Must stay in sync with RELEASE_MARKER in kernel_manager.py.
 */
const char ir0_version_release[] = "IR0VER:" IR0_VERSION_STRING;
