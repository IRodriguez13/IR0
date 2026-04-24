/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: kernel.h
 * Description: IR0 kernel source/header file
 */

#pragma once

/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: kernel.h
 * Description: Main kernel entry point and core function declarations
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Main kernel entry point. multiboot_info is physical addr of Multiboot info (ebx), may be 0. */
void kmain(uint32_t multiboot_info);

// Core subsystem initialization functions
void gdt_install(void);
void setup_tss(void);
void logging_init(void);
void ps2_init(void);
void keyboard_init(void);
void simple_alloc_init(void);
void ata_init(void);
int vfs_init_with_minix(void);
int vfs_init_root(void);
void process_init(void);
int clock_system_init(void);
void syscalls_init(void);
void idt_init64(void);
void idt_load64(void);
void pic_remap64(void);
void pic_unmask_irq(uint8_t irq);
int start_init_process(void);
void serial_init(void);
void heap_init(void);
bool sb16_init(void);