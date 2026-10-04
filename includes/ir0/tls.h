/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: tls.h
 * Description: User TLS base facade (x86 FS.base / ARM TPIDR_EL0 / RISC-V tp).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

void tls_set_base(uint64_t base);
uint64_t tls_get_base(void);
void tls_restore_current(void);

static inline void set_tls(uint64_t base)
{
	tls_set_base(base);
}

static inline void set_user_tls(uint64_t base)
{
	set_tls(base);
}

static inline void tls_invalidate(void)
{
}
