/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: irq.h
 * Description: Portable device IRQ request/dispatch facade.
 */

#pragma once

#include <stdint.h>

typedef void (*ir0_irq_fn)(uint32_t irq, void *data);

void irq_tables_init(void);
void irq_controller_init(void);
void irq_keyboard_init(void);
void irq_keyboard_poll_ps2(void);
void irq_unmask_line(unsigned irq);
void irq_init(void);
int irq_hw_enable(uint32_t irq);
int irq_request(uint32_t irq, ir0_irq_fn fn, void *data, const char *name);
int irq_dispatch(uint32_t irq);
