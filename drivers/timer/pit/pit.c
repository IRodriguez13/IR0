/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025 Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: pit.c
 * Description: PIT and PIC initialization
 */

#include "pit.h"
#include <ir0/oops.h>
#include <ir0/vga.h>
#include <arch_interface.h>
#include <arch/common/idt.h>
#include <vga.h>
#include <kernel/rr_sched.h>
#include <arch/x86-64/sources/arch_x64.h>
#include <ir0/driver.h>
#include <ir0/logging.h>

/* Driver registration structures */
static int32_t pit_hw_init(void)
{
    /* PIT is already initialized by init_PIT, but we can return 0 */
    return 0;
}

static ir0_driver_ops_t pit_ops = {
    .init = pit_hw_init,
    .shutdown = NULL
};

static ir0_driver_info_t pit_info = {
    .name = "PIT Timer",
    .version = "1.0",
    .author = "Iván Rodriguez",
    .description = "Programmable Interval Timer Driver",
    .language = IR0_DRIVER_LANG_C
};

/* Global variable for PIT ticks */
uint64_t pit_ticks = 0;

extern void timer_stub();

static uint32_t ticks = 0;

uint32_t get_pit_ticks(void)
{
    return ticks;
}

void increment_pit_ticks(void)
{
    ticks++;
    /* Update clock system - this updates uptime_milliseconds */
    extern void clock_tick(void);
    clock_tick();
    
}

/* Initialize PIC (Programmable Interrupt Controller) */
void init_pic(void)
{
    /* Save current masks (not used for now) */
    (void)inb(PIC1_DATA); /* Read current mask1 */
    (void)inb(PIC2_DATA); /* Read current mask2 */

    /* Initially disable all interrupts */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);

    /* ICW1: Initialization */
    outb(PIC1_COMMAND, PIC_ICW1_INIT); /* ICW1 for PIC1 */
    outb(PIC2_COMMAND, PIC_ICW1_INIT); /* ICW1 for PIC2 */

    /* ICW2: Vector offset */
    outb(PIC1_DATA, 0x20); /* PIC1: IRQ 0-7 -> INT 0x20-0x27 */
    outb(PIC2_DATA, 0x28); /* PIC2: IRQ 8-15 -> INT 0x28-0x2F */

    /* ICW3: Cascade */
    outb(PIC1_DATA, 0x04); /* PIC1: IRQ2 connected to PIC2 */
    outb(PIC2_DATA, 0x02); /* PIC2: Cascada a IRQ2 de PIC1 */

    /* ICW4: 8086 Mode */
    outb(PIC1_DATA, PIC_ICW4_8086); /* PIC1: 8086 mode */
    outb(PIC2_DATA, PIC_ICW4_8086); /* PIC2: 8086 mode */

    /* Stable mode: Keep all interrupts disabled */
    outb(PIC1_DATA, 0xFF); /* PIC1: All disabled */
    outb(PIC2_DATA, 0xFF); /* PIC2: All disabled */
}

void init_PIT(uint32_t frequency)
{
    LOG_INFO_FMT("PIT", "Registering PIT Timer at %d Hz...", frequency);
    ir0_register_driver(&pit_info, &pit_ops);

    /* Initialize PIC first */
    init_pic();

    /* Calculate divisor for the desired frequency */
    uint32_t divisor = PIT_BASE_FREC / frequency;

    /* Configure PIT */
    outb(PIT_REG_COMMAND, PIT_COMMAND_VAL);    /* Command: canal 0, lohi, modo 3 */
    outb(PIT_REG_CHAN0, divisor & 0xFF);       /* Low byte of divisor */
    outb(PIT_REG_CHAN0, (divisor >> 8) & 0xFF); /* High byte of divisor */

    /* Enable timer interrupt (IRQ 0) */
    uint8_t mask = inb(PIC1_DATA);
    mask &= ~(1 << 0); /* Enable IRQ 0 (timer) */
    outb(PIC1_DATA, mask);
}
