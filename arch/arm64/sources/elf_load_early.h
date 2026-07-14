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

/** Load embedded BusyBox and run echo applet (noreturn on OK). */
int arm64_busybox_el0(void);

/** Second drop after echo: fake /init smoke + echo ARM64_BUSYBOX_INIT_OK. */
int arm64_busybox_init_el0(void);

int arm64_busybox_mode(void);
int arm64_busybox_wrote(void);
void arm64_busybox_note_write(const char *buf, uint64_t len);
void arm64_after_busybox(void);
