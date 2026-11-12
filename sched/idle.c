/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: idle.c
 * Description: The idle task: poll interrupt-less devices, run woken tasks,
 *              halt until the next interrupt.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/bluetooth.h>
#include <ir0/cpu.h>
#include <ir0/idle.h>
#include <ir0/input_backend.h>
#include <ir0/net.h>
#include <ir0/sched.h>

/*
 * Devices that may not raise an interrupt: NICs without an RX IRQ, the
 * Bluetooth HCI UART, and PS/2 bytes an emulator delivered without IRQ1.
 * Each poll hands what it finds to its producer path, which wakes the
 * waiting tasks. Builds without networking or Bluetooth link a no-op.
 * Sleeping tasks never call this: they wait in schedule() for a wake.
 */
static void idle_poll(void)
{
	net_stack_poll();
	ir0_bluetooth_poll();
	input_kbd_poll_ps2();
}

void kernel_idle_loop(void)
{
	for (;;)
	{
		enable_interrupts();
		idle_poll();
		if (sched_count_runnable() > 1)
			schedule();
		else
			cpu_idle();
	}
}
