/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * FASE58K — #UD exception diagnostics (fault path only).
 */

#pragma once

#include <stdint.h>

void ir0_fase58j_note_syscall(uint64_t nr);
void ir0_fase58j_note_irq(uint64_t vector);
void ir0_fase58j_ud_fault_report(uint64_t *stack);
