// SPDX-License-Identifier: GPL-3.0-only
/*
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: main.c
 * Description: kmain — kernel boot convergence point.
 *
 *    00000000: 01010011 01101001 01100011 00100000 01110000 01100001
 *    00000006: 01110010 01110110 01101001 01110011 00100000 01101101
 *    0000000c: 01100001 01100111 01101110 01100001
 *
 */

#include <ir0/driver.h>
#include <ir0/kernel.h>
#include <kernel/boot/boot_init.h>

/*
 * kmain - where every ISA converges after its entry code.
 *
 * Each call brings up one subsystem, in boot order; the helpers in
 * kernel/boot/ carry the logging and the optional parts.
 */
void kmain(uintptr_t firmware_handle)
{
	/* Boot parameters, firmware tables and the command line. */
	boot_firmware_init(firmware_handle);

	/* Early CPU state, kernel heap and the boot console. */
	boot_console_init();

	/* Physical memory manager and the kernel address space. */
	boot_memory_init();

	/* Kernel log ring. */
	boot_log_init();

	/* Driver registry, then the serial console and the core report. */
	ir0_driver_registry_init();
	boot_serial_init();

	/* Drivers and the root filesystem. */
	boot_drivers_init();
	boot_rootfs_init();

	/* Processes, IPC, clock and scheduler, system calls. */
	boot_process_init();
	boot_ipc_init();
	boot_clock_init();
	boot_syscalls_init();

	/* Interrupts on: the kernel core is up. */
	boot_irq_init();

	/* KTM checks and the in-kernel test suite when built in. */
	boot_diagnostics();

	/* Exec /sbin/init as PID 1; does not return. */
	boot_enter_userspace();
}
