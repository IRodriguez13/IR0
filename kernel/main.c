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
#include <drivers/serial/serial.h>
#include <ir0/kmem.h>
#include <mm/pmm.h>
#include <init.h>
#include <arch/common/arch_portable.h>
#include <arch/x86-64/sources/user_mode.h>
#include <rr_sched.h>
#include <config.h>
#include <kernel/elf_loader.h>
#include <drivers/timer/clock_system.h>
#include <drivers/init_drv.h>
#include <drivers/storage/block_dev.h>
#include "ipc.h"
#include "syscalls.h"

/* Include kernel header with all function declarations */
#include "kernel.h"

void kmain(uint32_t multiboot_info)
{
    /* Initialize architecture-specific early features (GDT, TSS, etc.) */
    arch_early_init();

    /* Banner */
    print("IR0 Kernel v0.0.1 Boot routine\n");

    /* Initialize core subsystems first (need heap for registration) */
    heap_init();

    /* VBE framebuffer from Multiboot (OSDev). Requires gfxpayload in grub.cfg. */
    {
        extern int vbe_init_from_multiboot(uint32_t);
        extern int vbe_init(void);
        if (vbe_init_from_multiboot(multiboot_info) != 0)
            vbe_init();  /* Fallback: VGA text mode for /dev/fb0 */
    }
    
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

    /* Check block device availability before filesystem init */
    if (!block_dev_is_present("hda"))
    {
        serial_print("[BOOT] WARNING: No block device hda detected\n");
        serial_print("[BOOT] Filesystem initialization may fail\n");
    }
    else
    {
        serial_print("[BOOT] Block device hda detected, proceeding with filesystem init\n");
    }

    /* Initialize filesystem */
    vfs_init_with_minix();
    log_subsystem_ok("FILESYSTEM");

    /* Initialize process management */
    process_init();
    log_subsystem_ok("PROCESSES");
    
    /* Initialize IPC subsystem */
    ipc_init();
    log_subsystem_ok("IPC");

    /* Initialize scheduler (Round Robin scheduler)
     * Round Robin provides fair time-sharing among processes
     * Alternative schedulers (CFS, Priority) are available but not activated
     * See kernel/scheduler/ for scheduler implementations
     */
    clock_system_init();

    /* Initialize system calls */
    syscalls_init();
    log_subsystem_ok("SYSCALLS");

    /* Initialize architecture-specific interrupt system (IDT, PIC, etc.) */
    arch_interrupt_init();

    /* Enable interrupts globally */
    arch_enable_interrupts();
    serial_print("[BOOT] Interrupts enabled globally (sti)\n");

    log_subsystem_ok("INTERRUPTS");

    /*
     * Executor al estilo KUnit: tests in-kernel al arranque (kernel-x64-test.bin).
     * Los tests que necesitan proceso se marcan SKIP si current_process == NULL.
     */
    {
        extern void kernel_test_run_all(void) __attribute__((weak));
        if (kernel_test_run_all)
            kernel_test_run_all();
    }

#if KERNEL_DEBUG_SHELL
    /* Init de test: shell integrada como PID 1. No es el init real (/sbin/init). */
    start_init_process();
    log_subsystem_ok("DEBUG_SHELL");
#else
    /* Init real: cargar /sbin/init desde el filesystem */
    serial_print("SERIAL: kmain: Loading userspace init...\n");
    if (kexecve("/sbin/init", NULL, NULL) < 0) 
    {
        serial_print("SERIAL: kmain: FAILED to load /sbin/init, falling back to debug shell\n");
        panic("Failed to load /sbin/init");
    }
#endif

    for (;;)
    {
        /* Poll network devices for incoming packets */
        net_poll();
        /* Process Bluetooth HCI events (inquiry results, etc.) so devices
         * appear during scan without needing to read /proc/bluetooth/devices */
        bluetooth_poll();
        /* Despertar procesos bloqueados en poll() cuando hay datos o timeout */
        poll_wake_check();
        __asm__ volatile("hlt");
    }
}