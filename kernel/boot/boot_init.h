/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: boot_init.h
 * Description: Boot steps that kmain() (kernel/main.c) calls in order.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

/* Typed boot parameters, arch firmware setup, firmware sysfs, log profile. */
void boot_firmware_init(uintptr_t firmware_handle);

/* Early CPU state, kernel heap, framebuffer or text console. */
void boot_console_init(void);

/* PMM from the firmware memory inventory; pin the kernel address space. */
void boot_memory_init(void);

/* Kernel log ring, ahead of the serial port. */
void boot_log_init(void);

/* Serial console once the driver registry exists; core boot report. */
void boot_serial_init(void);

/* Selectable drivers. */
void boot_drivers_init(void);

/* Root block device check, VFS root, bare-metal sentinel, embedded init. */
void boot_rootfs_init(void);

/* Process table, swap, exit policy. */
void boot_process_init(void);

/* Kernel IPC channels. */
void boot_ipc_init(void);

/* Clock sources and the scheduler policy check. */
void boot_clock_init(void);

/* Syscall entry and the syscall table. */
void boot_syscalls_init(void);

/* IRQ tables and controller, unmask lines, enable interrupts. */
void boot_irq_init(void);

/* KTM, optional hostshare log, in-kernel ktests. */
void boot_diagnostics(void);

/* Load /sbin/init, spawn idle, first schedule; panics instead of returning. */
void boot_enter_userspace(void);
