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
#include <ir0/version.h>
#include <kernel/elf_loader.h>
#include <drivers/timer/clock_system.h>
#include <drivers/init_drv.h>
#include <drivers/storage/block_dev.h>
#if CONFIG_ENABLE_VBE
#include <drivers/video/vbe.h>
#endif
#include <ir0/multiboot.h>
#include "ipc.h"
#include "syscalls.h"
#if CONFIG_ENABLE_NETWORKING
#include <ir0/net.h>
#include <drivers/net/rtl8139.h>
#endif
#if CONFIG_ENABLE_BLUETOOTH
#include <drivers/bluetooth/bluetooth_init.h>
#endif

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
        extern void console_init(void);
        extern void console_clear(uint8_t);
        extern int console_use_framebuffer(void);
#if CONFIG_ENABLE_VBE
        extern int vbe_init_from_multiboot(uint32_t);
        extern int vbe_init(void);
        if (vbe_init_from_multiboot(multiboot_info) != 0)
            vbe_init();  /* Fallback: VGA text mode for /dev/fb0 */
#endif
        console_init();
        if (console_use_framebuffer())
            console_clear(0x0F);  /* Black background, ready for text */
    }

    /* Banner (now uses framebuffer if available) */
    print("IR0 Kernel v" IR0_VERSION_STRING " Boot routine\n");
    
    /* Initialize driver subsystem (includes driver registry and multi-language drivers) */
    drivers_init();
    
    /*
     * Physical Memory Manager: manage frames in [32MB, 48MB).
     * Heap occupies [8MB, 32MB), so PMM must use disjoint memory.
     * Boot page tables identity-map up to 48MB (24 x 2MB pages).
     */
    pmm_init(PMM_PHYS_BASE, PMM_PHYS_SIZE);
    
    logging_init();
    serial_init();

    /*
     * Log console mode for debugging (serial now available).
     * If framebuffer init failed, dump multiboot info to diagnose.
     */
#if CONFIG_ENABLE_VBE
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
#endif

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

    /* Initialize architecture-specific interrupt system (IDT, PIC remap) */
    arch_interrupt_init();

    /*
     * Unmask IRQ lines after PIC is fully initialized.
     * IRQ2 (cascade) is required for any slave PIC line (8-15) to work.
     */
    {
        extern void pic_unmask_irq(uint8_t irq);
        pic_unmask_irq(0);   /* Timer (PIT) */
        pic_unmask_irq(1);   /* Keyboard */
        pic_unmask_irq(2);   /* Cascade — required for slave IRQs 8-15 */
#if CONFIG_ENABLE_NETWORKING
        {
            int rtl_irq = rtl8139_get_irq_line();
            if (rtl_irq >= 0 && rtl_irq < 16)
                pic_unmask_irq((uint8_t)rtl_irq);
        }
#endif
#if CONFIG_ENABLE_MOUSE
        pic_unmask_irq(12);  /* PS/2 Mouse */
#endif
    }

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
    {
        int init_ret = start_init_process();
        if (init_ret < 0)
            panic("Failed to start debug shell init process");
        panic("start_init_process returned unexpectedly");
    }
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
#if CONFIG_ENABLE_NETWORKING
        net_poll();
#endif
#if CONFIG_ENABLE_BLUETOOTH
        bluetooth_poll();
#endif
        /* Despertar procesos bloqueados en poll() cuando hay datos o timeout */
        poll_wake_check();
        /* Despertar procesos bloqueados en nanosleep() cuando expira el tiempo */
        sleep_wake_check();
        /* Despertar procesos bloqueados en read(0) cuando hay tecla */
        stdin_wake_check();
        __asm__ volatile("hlt");
    }
}