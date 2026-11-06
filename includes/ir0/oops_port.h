/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: oops_port.h
 * Description: ISA extra panic diagnostics — weak noop in arch/common.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void ir0_oops_dump_arch_extra(void);

#ifdef __cplusplus
}
#endif
