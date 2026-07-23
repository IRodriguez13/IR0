/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: switch_early.h
 * Description: Freestanding ARM64 EL1 cooperative CPU switch (F7h).
 */

#pragma once

#include <stdint.h>

/**
 * Callee-saved GPRs + SP + LR layout for arm64_cpu_switch (must match .S).
 */
struct arm64_cpu_ctx
{
	uint64_t x19;
	uint64_t x20;
	uint64_t x21;
	uint64_t x22;
	uint64_t x23;
	uint64_t x24;
	uint64_t x25;
	uint64_t x26;
	uint64_t x27;
	uint64_t x28;
	uint64_t x29;
	uint64_t x30;
	uint64_t sp;
};

/** Save @prev callee-saved state; restore @next and return into @next's LR. */
void arm64_cpu_switch(struct arm64_cpu_ctx *prev, struct arm64_cpu_ctx *next);

/**
 * Activate @next_ttbr (if non-zero) via mm_activate, then arm64_cpu_switch.
 */
void arm64_cpu_switch_mm(struct arm64_cpu_ctx *prev, struct arm64_cpu_ctx *next,
			 uint64_t next_ttbr);

/**
 * Two-stack cooperative switch demo. Prints ARM64_SWITCH_B then ARM64_SWITCH_OK.
 */
void arm64_switch_early_smoke(void);
