/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <asm-generic/elf.h>

/* Where an ET_DYN program (PIE) is loaded, below the user stack. */
#define IR0_ARCH_ELF_ET_DYN_BASE 0x60000000ULL
