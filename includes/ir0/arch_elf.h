/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_elf.h
 * Description: ELF e_machine accepted by this kernel build (portable loader).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

/** Return non-zero when @machine belongs to the selected ISA backend. */
int elf_machine_supported(uint16_t machine);

/** Return non-zero when @type is the backend's base-relative relocation. */
int elf_reloc_is_relative(uint32_t type);

/** Resolve a local ELF64 relocation according to the selected ISA ABI. */
int elf_local_reloc_value(uint32_t type, uint64_t load_bias,
			  uint64_t sym_value, int64_t addend,
			  uint64_t *out);
