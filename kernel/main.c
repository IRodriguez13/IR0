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
#include <drivers/video/vbe.h>
#include <ir0/multiboot.h>
#include "ipc.h"
#include "syscalls.h"
#include <ir0/net.h>
#include <drivers/bluetooth/bluetooth_init.h>

/* Include kernel header with all function declarations */
#include "kernel.h"

void kmain(uint32_t multiboot_info)
{
    /* Initialize architecture-specific early features (GDT, TSS, etc.) */
    arch_early_init();

    /* Initialize core subsystems first (need heap for VBE mapping) */
    heap_init();

    /*
     * VBE framebuffer from Multiboot. Must run before first print() so
     * that print() uses framebuffer when gfxpayload=1024x768x32 in grub.
     */
    {
        extern int vbe_init_from_multiboot(uint32_t);
        extern int vbe_init(void);
        if (vbe_init_from_multiboot(multiboot_info) != 0)
            vbe_init();  /* Fallback: VGA text mode for /dev/fb0 */
        extern void console_init(void);
        extern void console_clear(uint8_t);
        extern int console_use_framebuffer(void);
        console_init();
        if (console_use_framebuffer())
            console_clear(0x0F);  /* Black background, ready for text */
    }

    /* Banner (now uses framebuffer if available) */
    print("IR0 Kernel v0.0.1 Boot routine\n");
    
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

    /*
     * Log console mode for debugging (serial now available).
     * If framebuffer init failed, dump multiboot info to diagnose.
     */
    {
        extern int console_use_framebuffer(void);
        uint32_t w = 0, h = 0, bpp = 0;
        if (console_use_framebuffer() && vbe_get_info(&w, &h, &bpp))
        {
            log_info_fmt("BOOT", "Console: framebuffer %ux%ux%u", (unsigned)w, (unsigned)h, (unsigned)bpp);
        }
        else
        {
            serial_print("[BOOT] Console: VGA text (80x25)");
            if (vbe_is_available())
                serial_print(" [vbe fallback - may not be visible in graphics mode]");
            serial_print("\n");
            serial_print("[BOOT] vbe_fail_reason=");
            serial_print_hex32((uint32_t)vbe_fail_reason);
            serial_print(" (1=mb_null 2=no_fb 3=bad_dims 4=map_fail)\n");
            /* Diagnose: why did vbe_init_from_multiboot fail? */
            if (multiboot_info)
            {
                const struct multiboot_info *mb = (const struct multiboot_info *)(uintptr_t)multiboot_info;
                serial_print("[BOOT] Multiboot flags=0x");
                serial_print_hex32(mb->flags);
                serial_print(" (bit12=FB:");
                serial_print((mb->flags & (1u << 12)) ? "1" : "0");
                serial_print(") addr=0x");
                serial_print_hex32((uint32_t)(mb->framebuffer_addr & 0xFFFFFFFF));
                serial_print(" w=");
                serial_print_hex32(mb->framebuffer_width);
                serial_print(" h=");
                serial_print_hex32(mb->framebuffer_height);
                serial_print(" bpp=");
                serial_print_hex32(mb->framebuffer_bpp);
                serial_print("\n");
            }
            else
                serial_print("[BOOT] Multiboot info is NULL\n");
        }
    }

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
        /* Despertar procesos bloqueados en nanosleep() cuando expira el tiempo */
        sleep_wake_check();
        /* Despertar procesos bloqueados en read(0) cuando hay tecla */
        stdin_wake_check();
        __asm__ volatile("hlt");
    }
}