/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: all_objs_mark.c
 * Description: Marker TU forced into kernel-arm64-all.bin link.
 */

#include "pl011.h"
#include <ir0/boot_log.h>

void arm64_all_objs_mark(void)
{
	ir0_boot_smoke("ARM64_ALL_OBJS_LINK_OK");
}
