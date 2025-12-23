// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: main.c
 * Description: Kernel initialization and user-space transition routines
 */

#include <ir0/vga.h>
#include <ir0/oops.h>
#include <ir0/logging.h>
#include <stdbool.h>
#include <stdint.h>
#include <drivers/IO/ps2_mouse.h>
#include <drivers/audio/sound_blaster.h>
#include <ir0/memory/kmem.h>
#include <drivers/net/rtl8139.h>
#include <init.h>
#include <arch/x86-64/sources/user_mode.h>
#include <rr_sched.h>
#include <config.h>
#include <kernel/elf_loader.h>

// Include kernel header with all function declarations
#include "kernel.h"

void kmain(void)
{
    // Initialize GDT and TSS first
    gdt_install();
    setup_tss();

    // Banner
    print("IR0 Kernel v0.0.1 Boot routine\n");
    delay_ms(2500);

    // Initialize core subsystems first (need heap for registration)
    heap_init();
    logging_init();
    serial_init();

    log_subsystem_ok("CORE");

    // Initialize PS/2 controller and keyboard
    ps2_init();
    keyboard_init();
    pic_unmask_irq(1); // Enable keyboard IRQ
    log_subsystem_ok("PS2_KEYBOARD");

    // Initialize PS/2 mouse
    ps2_mouse_init();
    log_subsystem_ok("PS2_MOUSE");

    // Initialize Sound Blaster audio
    sb16_init();
    log_subsystem_ok("AUDIO_SB16");

    // Initialize storage
    ata_init();
    log_subsystem_ok("STORAGE");

    // Initialize network card
    rtl8139_init();
    log_subsystem_ok("NET_RTL8139");

    // Initialize filesystem
    vfs_init_with_minix();
    log_subsystem_ok("FILESYSTEM");

    // Initialize process management
    process_init();
    log_subsystem_ok("PROCESSES");

    // Initialize scheduler (using Round Robin for now)
    clock_system_init();

    // Initialize system calls
    syscalls_init();
    log_subsystem_ok("SYSCALLS");

    // Set up interrupts
    idt_init64();
    idt_load64();
    pic_remap64();

    log_subsystem_ok("INTERRUPTS");

    // panic("Test"); Just for testing

#if KERNEL_DEBUG_SHELL
    start_init_process();
    log_subsystem_ok("DEBUG_SHELL");
#else
    serial_print("SERIAL: kmain: Loading userspace init...\n");
    if (elf_load_and_execute("/bin/init") < 0) 
    {
        serial_print("SERIAL: kmain: FAILED to load /bin/init, falling back to debug shell\n");
        panic("Failed to load /bin/init");
    }
#endif


    for (;;)
    {
        __asm__ volatile("hlt"); // fallback if something goes wrong
    }
}