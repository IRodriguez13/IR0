/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: pic.c
 * Description: IR0 kernel source/header file
 */

#include "pic.h"
#include "io.h"
#include <ir0/vga.h>

/*
 * pic_remap64 - Remap PIC vectors to IRQ_BASE_MASTER..IRQ_BASE_SLAVE+7
 * and mask all IRQs.
 *
 * After this call every IRQ is masked. Drivers/subsystems unmask
 * individual lines via pic_unmask_irq() once the IDT is ready.
 */
void pic_remap64(void)
{
    outb(PIC1_COMMAND, PIC_ICW1_INIT);
    io_wait();
    outb(PIC1_DATA, IRQ_BASE_MASTER);
    io_wait();
    outb(PIC1_DATA, PIC_ICW3_MASTER);
    io_wait();
    outb(PIC1_DATA, PIC_ICW4_8086);
    io_wait();

    outb(PIC2_COMMAND, PIC_ICW1_INIT);
    io_wait();
    outb(PIC2_DATA, IRQ_BASE_SLAVE);
    io_wait();
    outb(PIC2_DATA, PIC_ICW3_SLAVE);
    io_wait();
    outb(PIC2_DATA, PIC_ICW4_8086);
    io_wait();

    /* Start with all IRQs masked; callers unmask as needed */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

/* Send EOI (64-bit PIC) */
void pic_send_eoi64(uint8_t irq)
{
    if (irq >= 8)
    {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI);
}

/* Mask one IRQ line */
void pic_mask_irq(uint8_t irq)
{
    uint16_t port;
    uint8_t value;

    if (irq < 8)
    {
        port = PIC1_DATA;
    }
    else
    {
        port = PIC2_DATA;
        irq -= 8;
    }

    value = inb(port) | (1 << irq);
    outb(port, value);
}

/* Unmask one IRQ line */
void pic_unmask_irq(uint8_t irq)
{
    uint16_t port;
    uint8_t value;

    if (irq < 8)
    {
        port = PIC1_DATA;
    }
    else
    {
        port = PIC2_DATA;
        irq -= 8;
    }

    value = inb(port) & ~(1 << irq);
    outb(port, value);
}
