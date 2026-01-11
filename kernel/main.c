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
#include <drivers/IO/pc_speaker.h>
#include <drivers/audio/sound_blaster.h>
#include <drivers/audio/adlib.h>
#include <drivers/serial/serial.h>
#include <ir0/memory/kmem.h>
#include <ir0/memory/pmm.h>
#include <ir0/net.h>
#include <init.h>
#include <arch/x86-64/sources/user_mode.h>
#include <rr_sched.h>
#include <config.h>
#include <kernel/elf_loader.h>
#include <drivers/timer/clock_system.h>
#include <interrupt/arch/pic.h>
#include <drivers/init_drv.h>

/* Include kernel header with all function declarations */
#include "kernel.h"

/**
 * init_all_drivers - Initialize all hardware drivers
 * 
 * This function initializes all hardware drivers in the correct order
 * and logs the initialization process via serial output.
 */
static void init_all_drivers(void)
{
    serial_print("[DRIVERS] Initializing all hardware drivers...\n");
    
    /* Initialize PS/2 controller and keyboard */
    serial_print("[DRIVERS] Initializing PS/2 controller and keyboard...\n");
    ps2_init();
    keyboard_init();
    /* Enable keyboard IRQ */
    pic_unmask_irq(1);
    log_subsystem_ok("PS2_KEYBOARD");
    serial_print("[DRIVERS] PS/2 keyboard initialized\n");
    
    /* Initialize PS/2 mouse */
    serial_print("[DRIVERS] Initializing PS/2 mouse...\n");
    ps2_mouse_init();
    log_subsystem_ok("PS2_MOUSE");
    serial_print("[DRIVERS] PS/2 mouse initialized\n");
    
    /* Initialize PC Speaker */
    serial_print("[DRIVERS] Initializing PC Speaker...\n");
    pc_speaker_init();
    log_subsystem_ok("PC_SPEAKER");
    serial_print("[DRIVERS] PC Speaker initialized\n");
    
    /* Initialize audio drivers */
    serial_print("[DRIVERS] Initializing audio drivers...\n");
    sb16_init();
    log_subsystem_ok("AUDIO_SB16");
    serial_print("[DRIVERS] Sound Blaster 16 initialized\n");
    
    adlib_init();
    log_subsystem_ok("AUDIO_ADLIB");
    serial_print("[DRIVERS] Adlib OPL2 initialized\n");
    
    /* Initialize storage */
    serial_print("[DRIVERS] Initializing storage drivers...\n");
    ata_init();
    log_subsystem_ok("STORAGE");
    serial_print("[DRIVERS] ATA/IDE storage initialized\n");
    
    /* Initialize network stack (drivers + protocols) */
    serial_print("[DRIVERS] Initializing network stack...\n");
    init_net_stack();
    /* Enable RTL8139 IRQ */
    pic_unmask_irq(11);
    log_subsystem_ok("NETWORK_STACK");
    serial_print("[DRIVERS] Network stack initialized\n");
    
    serial_print("[DRIVERS] All drivers initialized successfully\n");
}

void kmain(void)
{
    /* Initialize GDT and TSS first */
    gdt_install();
    setup_tss();

    /* Banner */
    print("IR0 Kernel v0.0.1 Boot routine\n");

    /* Initialize core subsystems first (need heap for registration) */
    heap_init();
    
    /* Initialize driver subsystem (includes driver registry and multi-language drivers) */
    drivers_init();
    
    /* Initialize Physical Memory Manager (PMM)
     * Manage physical frames in the 32MB region (8MB-32MB)
     * This gives us ~24MB of physical memory frames
     * 8MB start, 24MB size
     */
    pmm_init(0x800000, 0x1800000);
    
    logging_init();
    serial_init();

    log_subsystem_ok("CORE");

    /* Initialize all hardware drivers */
    init_all_drivers();

    /* Initialize filesystem */
    vfs_init_with_minix();
    log_subsystem_ok("FILESYSTEM");

    /* Initialize process management */
    process_init();
    log_subsystem_ok("PROCESSES");

    /* Initialize scheduler (using Round Robin for now) */
    clock_system_init();

    /* Initialize system calls */
    syscalls_init();
    log_subsystem_ok("SYSCALLS");

    /* Set up interrupts */
    idt_init64();
    idt_load64();
    pic_remap64();

    __asm__ volatile("sti");
    serial_print("[BOOT] Interrupts enabled globally (sti)\n");

    log_subsystem_ok("INTERRUPTS");

    /* panic("Test"); Just for testing */

#if KERNEL_DEBUG_SHELL
    start_init_process();
    log_subsystem_ok("DEBUG_SHELL");
#else
    serial_print("SERIAL: kmain: Loading userspace init...\n");
    if (elf_load_and_execute("/sbin/init") < 0) 
    {
        serial_print("SERIAL: kmain: FAILED to load /sbin/init, falling back to debug shell\n");
        panic("Failed to load /sbin/init");
    }
#endif

    for (;;)
    {
        /* Fallback if something goes wrong */
        __asm__ volatile("hlt");
    }
}