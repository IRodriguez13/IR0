/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: elf_load_early.h
 * Description: Generic ELF64 EL0 enter plus one active guest continuation
 */

#pragma once

#include <stdint.h>
#include <ir0/early_elf_stack.h>

typedef void (*arm64_early_guest_exit_fn)(int wrote);

void arm64_early_guest_begin(arm64_early_guest_exit_fn on_exit);
int arm64_early_guest_active(void);
void arm64_early_guest_note_write(const char *buf, uint64_t len);
void arm64_early_guest_leave(void);

int arm64_early_elf_enter(uint64_t entry, uint64_t stack_top, uint64_t stack_size,
			  const struct ir0_early_elf_auxv *aux,
			  char *const argv[]);

/* First embedded ET_EXEC (build/hello_aarch64). */
int arm64_guest0_el0(void);

/* Second embedded ET_EXEC (build artifact, path+argv only). */
int arm64_embedded_elf_prepare(void);
void arm64_embedded_elf_enter(char *const argv[]);
int arm64_embedded_elf_el0(void);
int arm64_embedded_init_el0(void);
int arm64_init_handoff_early(void);
