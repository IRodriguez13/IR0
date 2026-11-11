/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
/*
 * open(2) flag bits that differ between ISAs: arch/arm64/include/uapi/asm/
 * fcntl.h. Selected by includes/ir0/open_flags.h.
 */
#define LINUX_O_DIRECTORY 0x004000U /* 040000 */
#define LINUX_O_NOFOLLOW  0x008000U /* 0100000 */
#define LINUX_O_LARGEFILE 0x020000U /* 0400000 */
