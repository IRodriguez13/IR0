/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: transport.h
 * Description: KTM host-visible output (serial protocol KTM|…).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

void ktm_transport_emit(const char *kind, const char *name, const char *status);
void ktm_transport_emit_u64(const char *kind, const char *name, uint64_t value);
void ktm_transport_suite_end(unsigned pass, unsigned fail);
