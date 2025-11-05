// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: isr_handlers.c
 * Description: Interrupt service routine handlers for x86 architecture
 */

#include "isr_handlers.h"
#include <ir0/print.h>

// Handler mínimo ya definido en idt.c
// void isr_handler(uint8_t int_no) está en idt.c

// Función simple para enviar EOI
void isr_send_eoi(uint8_t irq) 
{
    if (irq >= 8) 
    {
        // Send EOI to slave PIC
        __asm__ volatile("outb %%al, %%dx" : : "a"(0x20), "d"(0xA0));
    }
    // Send EOI to master PIC
    __asm__ volatile("outb %%al, %%dx" : : "a"(0x20), "d"(0x20));
}

// Handler de timer simple
void time_handler(void) 
{
    // Solo enviar EOI
    isr_send_eoi(0);
}