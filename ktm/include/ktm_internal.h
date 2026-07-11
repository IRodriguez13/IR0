/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ktm_internal.h
 * Description: Private KTM helpers (not a public facade).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/ktm/ktm.h>
#include <stddef.h>

struct ktm_writer
{
	char *buf;
	size_t cap;
	size_t used;
};

void ktm_registry_init(void);
ktm_context_t *ktm_current_context(void);
void ktm_set_current_context(ktm_context_t *ctx);

uint64_t ktm_now_ticks(void);
int32_t ktm_current_pid(void);
