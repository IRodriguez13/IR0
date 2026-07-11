/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: probe.h
 * Description: KTM probe registry (structured state queries).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct ktm_writer ktm_writer_t;

void ktm_write_u64(ktm_writer_t *w, const char *key, uint64_t value);
void ktm_write_cstr(ktm_writer_t *w, const char *key, const char *value);

typedef int (*ktm_probe_fn)(void *ctx, ktm_writer_t *writer);

int ktm_probe_register(const char *name, ktm_probe_fn fn, void *ctx);
int ktm_probe_run(const char *name, ktm_writer_t *writer);
void ktm_probes_register_builtins(void);
