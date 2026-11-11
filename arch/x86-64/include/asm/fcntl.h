/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
/*
 * open(2) flag bits that differ between ISAs: x86_64 uses
 * include/uapi/asm-generic/fcntl.h. Selected by includes/ir0/open_flags.h.
 */
#define LINUX_O_LARGEFILE 0x008000U /* 0100000 */
#define LINUX_O_DIRECTORY 0x010000U /* 0200000 */
#define LINUX_O_NOFOLLOW  0x020000U /* 0400000 */
