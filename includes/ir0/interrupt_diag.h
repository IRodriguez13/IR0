/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 interrupt/PIC/IDT query facade (FASE58J input diagnostics).
 */

#pragma once

#include <stdint.h>

uint64_t ir0_arch_read_rflags(void);
int ir0_arch_irq_masked(uint8_t irq);
uint64_t ir0_arch_idt_handler(uint8_t vector);
