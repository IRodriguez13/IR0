/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: elf_load_early.h
 * Description: Minimal ELF64 PT_LOAD + EL0 entry for embedded musl hello.
 */

#pragma once

#include <stdint.h>
#include <ir0/early_elf_stack.h>

/**
 * Parse embedded hello_aarch64, map EL0 pages, enter musl _start (noreturn on OK).
 * Prints ARM64_MUSL_LOAD_OK then erets; on failure prints ARM64_MUSL_LOAD_FAIL.
 */
int arm64_musl_hello_el0(void);

/** Nonzero while musl EL0 guest is running (exit returns to arm64_after_musl). */
int arm64_musl_mode(void);

/** Nonzero after musl wrote the hello banner via SVC write. */
int arm64_musl_hello_wrote(void);

/** Called from sys_write when buffer matches IR0_MUSL_AARCH64_HELLO_OK. */
void arm64_musl_note_write(const char *buf, uint64_t len);

/** EL1 continuation after musl exit_group/exit. */
void arm64_after_musl(void);

/**
 * Map-ready EL0 enter: SysV stack from caller argv, then eret.
 * Does not invent program names; argv/execfn come from the caller.
 */
int arm64_early_elf_enter(uint64_t entry, uint64_t stack_top, uint64_t stack_size,
			  const struct ir0_early_elf_auxv *aux,
			  char *const argv[]);

/** Load the embedded PID1 ELF blob (harness image, not a BusyBox policy). */
int arm64_busybox_el0(void);
int arm64_busybox_prepare(void);
void arm64_busybox_enter(char *const argv[]);
int arm64_init_handoff_early(void);

/** Second harness drop after the first EL0 guest exits. */
int arm64_busybox_init_el0(void);

int arm64_busybox_mode(void);
int arm64_busybox_wrote(void);
void arm64_busybox_note_write(const char *buf, uint64_t len);
void arm64_after_busybox(void);
