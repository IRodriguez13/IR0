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
 * Description: Kernel initialization and user-space transition routines
 *
 *    00000000: 01010011 01101001 01100011 00100000 01110000 01100001  
 *    00000006: 01110010 01110110 01101001 01110011 00100000 01101101  
 *    0000000c: 01100001 01100111 01101110 01100001                    
 * 
 */

#include <ir0/vga.h>
#include <ir0/oops.h>
#include <ir0/logging.h>
#include <stdbool.h>
#include <stdint.h>
#include <ir0/serial_io.h>
#include <ir0/kmem.h>
#include <mm/pmm.h>
#include <init.h>
#include <ir0/arch_port.h>
#include <config.h>
#include <ir0/version.h>
#include <ir0/driver.h>
#include <kernel/elf_loader.h>
#include <ir0/clock.h>
#include <ir0/init_drv.h>
#include <ir0/block_dev.h>
#include <ir0/video_backend.h>
#include <ir0/console_backend.h>
#include <ir0/console.h>
#include <ir0/multiboot.h>
#include <ir0/ktm/ktm.h>
#include "ipc.h"
#include "syscalls.h"
#include "scheduler_api.h"
#include "process.h"
#include "rootfs_base.h"

#if CONFIG_ENABLE_NETWORKING

#include <ir0/net.h>

#endif

#if CONFIG_ENABLE_BLUETOOTH
#include <ir0/bluetooth.h>
#endif

/* Include kernel header with all function declarations */
#include "kernel.h"

/*
 * kernel_idle_poll - Wake blocked tasks and poll optional subsystems.
 * Shared by the RR idle kernel process and the kmain fallback loop.
 */
void kernel_idle_poll(void)
{
#if CONFIG_ENABLE_NETWORKING
	net_stack_poll();
#endif
#if CONFIG_ENABLE_BLUETOOTH
	ir0_bluetooth_poll();
#endif
	poll_wake_check();
	sleep_wake_check();
	keyboard_poll_ps2();
	stdin_wake_check();
	pipe_wake_check();
	if (ir0_console_take_resched())
		sched_schedule_next();
}

/*
 * kernel_idle_loop - Always-runnable kernel task (Linux idle analogue).
 * Enqueued after /sbin/init so PID 1 runs first; takes over when init exits.
 */
void kernel_idle_loop(void)
{
	for (;;)
	{
		kernel_idle_poll();
		arch_cpu_idle();
	}
}

void kmain(uint32_t multiboot_info)
{
#if !CONFIG_ENABLE_VBE

    (void)multiboot_info;

#endif

    /* Initialize architecture-specific early features (GDT, TSS, etc.) */
    arch_set_boot_params((void *)(uintptr_t)multiboot_info);
    arch_early_init();

    /* Initialize core subsystems first (need heap for VBE mapping) */
    heap_init();

    /*
     * VBE framebuffer from Multiboot. Must run before first print() so
     * that print() uses framebuffer when gfxpayload=1024x768x32 in grub.
     */
    {

#if CONFIG_ENABLE_VBE

        if (video_backend_init_from_multiboot(multiboot_info) != 0)
            video_backend_init_fallback();  /* Fallback: VGA text mode for /dev/fb0 */

#endif

        console_backend_init();
        if (console_backend_uses_framebuffer())
            console_backend_clear(0x0F);  /* Black background, ready for text */
    }

    /* Banner (now uses framebuffer if available) */
    print("IR0 Kernel v" IR0_VERSION_STRING " Boot routine\n");
    
    /*
     * Physical Memory Manager: manage frames in [32MB, 48MB).
     * Heap occupies [8MB, 32MB), so PMM must use disjoint memory.
     * Boot page tables identity-map up to 48MB (24 x 2MB pages).
     */
    pmm_init(PMM_PHYS_BASE, PMM_PHYS_SIZE);
    
    logging_init();
    /*
     * Core driver policy:
     * serial/logging/clock/interrupt plumbing is always-on and initialized in
     * kmain. Selectable hardware stacks are initialized later via init_all_drivers().
     */
    ir0_driver_registry_init();
    serial_init();

    /*
     * Log console mode for debugging (serial now available).
     * If framebuffer init failed, dump multiboot info to diagnose.
     */
#if CONFIG_ENABLE_VBE
    {
        uint32_t w = 0, h = 0, bpp = 0;
        if (console_backend_uses_framebuffer() && video_backend_get_info(&w, &h, &bpp))
        {
            log_info_fmt("BOOT", "Console: framebuffer %ux%ux%u", (unsigned)w, (unsigned)h, (unsigned)bpp);
        }
        else
        {
#if DEBUG_BOOT
            serial_print("[BOOT] Console: VGA text (80x25)");
            if (video_backend_is_available())
                serial_print(" [vbe fallback - may not be visible in graphics mode]");
            serial_print("\n");
            serial_print("[BOOT] vbe_fail_reason=");
            serial_print_hex32((uint32_t)video_backend_fail_reason());
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
#endif
        }
    }
#endif

    log_subsystem_ok("CORE");

    /* Initialize all hardware drivers */
    init_all_drivers();

    /* Check configured root block device availability before filesystem init */
    if (!block_dev_is_present(CONFIG_ROOT_BLOCK_DEVICE))
    {
        serial_print("[BOOT] WARNING: Configured root block device not detected\n");
        serial_print("[BOOT] Filesystem initialization may fail\n");
    }
#if DEBUG_BOOT
    else
    {
        serial_print("[BOOT] Configured root block device detected, proceeding with filesystem init\n");
    }
#endif

    /* Initialize filesystem */
    vfs_init_root();
    log_subsystem_ok("FILESYSTEM");

    /* Initialize process management */

    process_init();
    log_subsystem_ok("PROCESSES");
    
    /* Initialize IPC subsystem */
    ipc_init();
    log_subsystem_ok("IPC");

    /* Scheduler tick + policy backend (see CONFIG_SCHEDULER_POLICY). */
    clock_system_init();
#if CONFIG_SCHEDULER_POLICY == 2
    {
	extern int priority_sched_selftest(void);

	if (priority_sched_selftest() == 0)
		serial_print("SCHED_PRIO_OK\n");
	else
		serial_print("SCHED_PRIO_FAIL\n");
    }
#endif
    serial_print("SCHED_POLICY=");
    serial_print(sched_active_policy_name());
    serial_print("\n");

    /* Initialize system calls */
    arch_syscall_init();
    syscalls_init();
    log_subsystem_ok("SYSCALLS");

    /* Initialize architecture-specific interrupt system (IDT, PIC remap) */
    arch_irq_init();
    arch_boot_irq_unmask();

    /* Enable interrupts globally */
    arch_enable_interrupts();
#if DEBUG_BOOT
    serial_print("[BOOT] Interrupts enabled globally (sti)\n");
#endif

    log_subsystem_ok("INTERRUPTS");

    ktm_core_init();
    KTM_CHECKPOINT(KTM_CP_BOOT_READY);
#if defined(CONFIG_KTM_TEST) && CONFIG_KTM_TEST
    ktm_scenarios_run_boot();
#endif

    /*
     * In-kernel tests run from init_1 (process context) when linked.
     * Calling them here (before init process exists) would force SKIP paths.
     */

#if KERNEL_DEBUG_SHELL
    /* Init de test: shell integrada como PID 1. No es el init real (/sbin/init). */
    {
        int init_ret = start_init_process();
        if (init_ret < 0)
            panic("Failed to start debug shell init process");
        panic("start_init_process returned unexpectedly");
    }
#else
    /* Real init: load /sbin/init from root filesystem and run in ring 3. */
    {
        pid_t init_pid;
        char *argv_init[] = { "/sbin/init", NULL };

#if DEBUG_BOOT
        serial_print("SERIAL: kmain: Loading userspace init...\n");
#endif
        ir0_rootfs_prepare_userspace_base();
        /* Linux-like: argv[0] must be present or BusyBox/runit print usage and exit. */
        process_prepare_pid1_for_init();
        init_pid = kexecve("/sbin/init", argv_init, NULL);
        if (init_pid < 0)
        {
            serial_print("SERIAL: kmain: FAILED to load /sbin/init\n");
            panic("Failed to load /sbin/init");
        }
#if DEBUG_BOOT
        serial_print("SERIAL: kmain: /sbin/init loaded (PID ");
        serial_print_hex32((uint32_t)init_pid);
        serial_print("), scheduling...\n");
#endif

        ir0_console_on_userspace_attach();

        sched_schedule_next();
        panic("sched_schedule_next returned after userspace init");
    }
#endif

    for (;;)
    {
        kernel_idle_poll();
        arch_cpu_idle();
    }
}
