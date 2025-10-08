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

#include <ir0/print.h>
#include <ir0/logging.h>
#include <stdbool.h>
#include <stdint.h>
#include <drivers/IO/ps2_mouse.h>
#include <drivers/audio/sound_blaster.h>


// Forward declarations for subsystem init functions
extern void gdt_install(void);
extern void setup_tss(void);
extern void logging_init(void);
extern void ps2_init(void);
extern void keyboard_init(void);
extern void simple_alloc_init(void);
extern void ata_init(void);
extern int vfs_init_with_minix(void);
extern void process_init(void);
extern int clock_system_init(void);
extern int scheduler_cascade_init(void);
extern void syscalls_init(void);
extern void idt_init64(void);
extern void idt_load64(void);
extern void pic_remap64(void);
extern void pic_unmask_irq(uint8_t irq);
extern int start_init_process(void);
extern void init_1(void);
extern void switch_to_user_mode(void *entry_point);

void kmain(void)
{
    // Initialize GDT and TSS first
    gdt_install();
    setup_tss();
    
    // Banner
    print("IR0 Kernel v0.0.1 Boot\n");
    delay_ms(2500);

    // Initialize logging
    logging_init();
    log_subsystem_ok("INIT");

    // Initialize serial for debugging
    extern void serial_init(void);
    serial_init();
    log_subsystem_ok("SERIAL");

    // Initialize PS/2 controller and keyboard
    ps2_init();
    keyboard_init();
    pic_unmask_irq(1);  // Enable keyboard IRQ
    log_subsystem_ok("PS2_KEYBOARD");

    // Initialize PS/2 mouse
    ps2_mouse_init();
    log_subsystem_ok("PS2_MOUSE");


    // Initialize memory allocator
    simple_alloc_init();
    log_subsystem_ok("MEMORY");

    // Initialize Sound Blaster audio
    sb16_init();
    log_subsystem_ok("AUDIO_SB16");


    // Initialize storage
    ata_init();
    log_subsystem_ok("STORAGE");

    // Initialize filesystem
    vfs_init_with_minix();
    log_subsystem_ok("FILESYSTEM");

    // Initialize process management
    process_init();
    log_subsystem_ok("PROCESSES");

    // Initialize scheduler
    clock_system_init();
    scheduler_cascade_init();
    log_subsystem_ok("SCHEDULER");

    // Initialize system calls
    syscalls_init();
    log_subsystem_ok("SYSCALLS");

    // Set up interrupts
    idt_init64();
    idt_load64();
    pic_remap64();
    log_subsystem_ok("INTERRUPTS");

    // Start init process and switch to user mode
    start_init_process();
    log_subsystem_ok("USERMODE");

    print("Switching to user mode...\n");
    delay_ms(2500);
    __asm__ volatile("sti");
    switch_to_user_mode((void*)init_1);

    // Should never return
    for (;;) __asm__ volatile("hlt");
}