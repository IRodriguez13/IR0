/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <asm-generic/elf.h>

/*
 * Linux arm64 ELF_ET_DYN_BASE is two thirds of the 48-bit user VA; a PIE
 * with 64 KiB segment alignment lands at 0xaaaaaaaa0000, as on Linux.
 */
#define IR0_ARCH_ELF_ET_DYN_BASE 0x0000AAAAAAAA0000ULL
