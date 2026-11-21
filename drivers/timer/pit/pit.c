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
#include <config.h>
#include <ir0/oops.h>
#include <arch_interface.h>
#include <ir0/driver.h>
#include <ir0/logging.h>
#include <ir0/clock.h>
#include <ir0/irq.h>
#include <ir0/percpu.h>
#include <ir0/process.h>
#include <ir0/resource_registry.h>
#include <asm/io.h>

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

/* The tick saves the interrupted user context before clock_tick(). */
static void pit_irq(uint32_t irq, void *data)
{
	(void)irq;
	(void)data;
	process_save_user_exception_frame(cpu_local_irq_frame());
	clock_note_irq();
	clock_tick();
}

/*
 * init_PIT - Configure PIT channel 0 at the given frequency.
 *
 * PIC initialization and IRQ unmasking is handled centrally
 * by pic_remap64() + pic_unmask_irq() in main.c.
 */
void init_PIT(uint32_t frequency)
{
    LOG_INFO_FMT("PIT", "Registering PIT Timer at %d Hz...", frequency);
    ir0_register_driver(&pit_info, &pit_ops);

    uint32_t divisor = PIT_BASE_FREC / frequency;

    outb(PIT_REG_COMMAND, PIT_COMMAND_VAL);
    outb(PIT_REG_CHAN0, divisor & 0xFF);
    outb(PIT_REG_CHAN0, (divisor >> 8) & 0xFF);

    resource_register_irq(0, "timer");
    if (irq_request(0, pit_irq, NULL, "timer") != 0)
        LOG_WARNING("PIT", "IRQ 0 already claimed");
    resource_register_ioport(PIT_REG_CHAN0, PIT_REG_COMMAND, "timer0");
}

clock_timer_t clock_timer_init(void)
{
	init_PIT(CONFIG_TICK_RATE_HZ);
	return CLOCK_TIMER_PIT;
}
