/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: isr_handlers.c
 * Description: Interrupt Service Routine handlers for exceptions, hardware interrupts, and system calls
 */

#include "idt.h"
#include "pic.h"
#include "io.h"
#include <ir0/vga.h>
#include <kernel/rr_sched.h>
#include <drivers/net/rtl8139.h>

/* Declaraciones externas para el nuevo driver de teclado */
extern void keyboard_handler64(void);
extern void keyboard_handler32(void);
extern void increment_pit_ticks(void);
#ifdef __x86_64__

/* Handler de interrupciones para 64-bit */
void isr_handler64(uint64_t interrupt_number)
{
    /* Manejar excepciones del CPU (0-31) */
    if (interrupt_number < 32)
    {
        print("Excepción CPU #");
        print_int32(interrupt_number);
        print("\n");

        /* Para excepciones, NO enviar EOI */
        return;
    }

    /* Manejar syscall (0x80) */
    if (interrupt_number == 128)
    {
        print("SYSCALL: Interrupción 0x80 recibida\n");
        return;
    }

    /* Manejar IRQs del PIC (32-47) */
    if (interrupt_number >= 32 && interrupt_number <= 47)
    {
        uint8_t irq = interrupt_number - 32;

        switch (irq)
        {
        case 0: /* Timer */
        {
            /* Increment PIT ticks and update clock system */
            /* clock_tick() will handle scheduler integration */
            increment_pit_ticks();
            break;
        }

        case 1: /* Keyboard */
            keyboard_handler64();
            break;

        case 11: /* RTL8139 Network Card */
            rtl8139_handle_interrupt();
            break;

        default:
            break;
        }

        /* Enviar EOI para IRQs */
        pic_send_eoi64(irq);
        return;
    }

}

#endif
