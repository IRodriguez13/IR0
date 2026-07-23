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
#include <string.h>
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
#include <ir0/blockdev.h>
#include <ir0/video_backend.h>
#include <ir0/console_backend.h>
#include <ir0/console.h>
#include <ir0/typewriter.h>
#include <ir0/input_backend.h>
#include <ir0/multiboot.h>
#include <ir0/ktm/ktm.h>
#include "ipc.h"
#include "syscalls.h"
#include <ir0/sched.h>
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
 * kernel_idle_poll_nosched - Same wakes as kernel_idle_poll without scheduling.
 * Used from clock_wait / blocked syscall loops that own a single yield point.
 */
void kernel_idle_poll_nosched(void)
{
#if CONFIG_ENABLE_NETWORKING
	net_stack_poll();
#endif
#if CONFIG_ENABLE_BLUETOOTH
	ir0_bluetooth_poll();
#endif
	(void)poll_wake_check_nosched();
	sleep_wake_check();
	input_kbd_poll_ps2();
	(void)stdin_wake_check_nosched();
	pipe_wake_check();
	(void)ir0_console_take_resched();
}

/*
 * kernel_idle_poll - Wake blocked tasks and poll optional subsystems.
 * Shared by the RR idle kernel process and the kmain fallback loop.
 */
void kernel_idle_poll(void)
{
	int woke = 0;

#if CONFIG_ENABLE_NETWORKING
	net_stack_poll();
#endif
#if CONFIG_ENABLE_BLUETOOTH
	ir0_bluetooth_poll();
#endif
	if (poll_wake_check_nosched())
		woke = 1;
	sleep_wake_check();
	input_kbd_poll_ps2();
	if (stdin_wake_check_nosched())
		woke = 1;
	pipe_wake_check();
	if (ir0_console_take_resched())
		woke = 1;
	if (woke)
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
		cpu_idle();
	}
}

void kmain(uint32_t multiboot_info)
{
#if !CONFIG_ENABLE_VBE

    (void)multiboot_info;

#endif

    /* Initialize architecture-specific early features (GDT, TSS, etc.) */
    set_boot_params((void *)(uintptr_t)multiboot_info);
    early_init();

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

    /* Version banner emitted after serial_init (klog + console). */

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
     * Banner must be the first framed klog line on serial (early ARCH/LOGGING/
     * DriverRegistry chatter is held via klog_boot_hold).
     */
    klog_boot_hold(0);
    klog_info("BOOT", "IR0 Kernel v" IR0_VERSION_STRING " Boot routine");
    typewriter_vga_print("IR0 Kernel v" IR0_VERSION_STRING " Boot routine\n", 0x0F);

    {
	char hv_vendor[16];

	if (arch_hypervisor_present() &&
	    arch_hypervisor_vendor(hv_vendor, sizeof(hv_vendor)) == 0)
	{
		/*
		 * CPUID 0x40000000 packs 12 chars (ebx|ecx|edx). QEMU TCG uses
		 * "TCGTCGTCGTCG"; KVM uses "KVMKVMKVM\0\0\0"; Xen "XenVMMXenVMM".
		 */
		if (strncmp(hv_vendor, "TCGTCGTCGTCG", 12) == 0)
			log_info_fmt("BOOT",
				     "hypervisor present vendor=%s (QEMU TCG)",
				     hv_vendor);
		else if (strncmp(hv_vendor, "KVMKVMKVM", 9) == 0)
			log_info_fmt("BOOT",
				     "hypervisor present vendor=%s (KVM)",
				     hv_vendor);
		else
			log_info_fmt("BOOT", "hypervisor present vendor=%s",
				     hv_vendor);
	}
	else
	{
		log_info("BOOT", "hypervisor none");
		log_info("BOOT", "IR0 is running on bare metal!");
		typewriter_vga_print("IR0 is running on bare metal!\n", 0x0A);
	}
    }

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
            log_info_fmt("BOOT",
                         "Console: VGA text (80x25)%s vbe_fail_reason=%u",
                         video_backend_is_available()
                             ? " [vbe fallback - may not be visible in graphics mode]"
                             : "",
                         (unsigned)video_backend_fail_reason());

            if (multiboot_info)
            {
                const struct multiboot_info *mb = (const struct multiboot_info *)(uintptr_t)multiboot_info;
                log_info_fmt("BOOT",
                             "Multiboot flags=0x%x (bit12=FB:%u) addr=0x%x w=%u h=%u bpp=%u",
                             (unsigned)mb->flags,
                             (mb->flags & (1u << 12)) ? 1u : 0u,
                             (unsigned)(mb->framebuffer_addr & 0xFFFFFFFFu),
                             (unsigned)mb->framebuffer_width,
                             (unsigned)mb->framebuffer_height,
                             (unsigned)mb->framebuffer_bpp);
            }
            else
                log_info("BOOT", "Multiboot info is NULL");
#endif
        }
    }
#endif

    log_subsystem_ok("CORE");

    /* Initialize all hardware drivers */
    init_all_drivers();

    /* Check configured root block device availability before filesystem init */
    if (!ir0_block_name_is_present(CONFIG_ROOT_BLOCK_DEVICE))
    {
        log_warn("BOOT", "Configured root block device not detected");
        log_warn("BOOT", "Filesystem initialization may fail");
    }
#if DEBUG_BOOT
    else
    {
        log_info("BOOT", "Configured root block device detected, proceeding with filesystem init");
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
		klog_smoke("SCHED_PRIO_OK");
	else
		klog_smoke("SCHED_PRIO_FAIL");
    }
#endif
    klog_info_fmt("SCHED", "SCHED_POLICY=%s", sched_active_policy_name());

    /* Initialize system calls */
    syscall_init();
    syscalls_init();
    log_subsystem_ok("SYSCALLS");

    /* Initialize architecture-specific interrupt system (IDT, PIC remap) */
    irq_init();
    boot_irq_unmask();

    /* Enable interrupts globally */
    enable_interrupts();
#if DEBUG_BOOT
    log_info("BOOT", "Interrupts enabled globally (sti)");
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
        log_info("BOOT", "Loading userspace init...");
#endif
        ir0_rootfs_prepare_userspace_base();
        /* Linux-like: argv[0] must be present or BusyBox/runit print usage and exit. */
        process_prepare_pid1_for_init();
        init_pid = kexecve("/sbin/init", argv_init, NULL);
        if (init_pid < 0)
        {
            log_error("BOOT", "FAILED to load /sbin/init");
            panic("Failed to load /sbin/init");
        }
#if DEBUG_BOOT
        log_info_fmt("BOOT", "/sbin/init loaded (PID %d), scheduling...", init_pid);
#endif

        ir0_console_on_userspace_attach();

        sched_schedule_next();
        panic("sched_schedule_next returned after userspace init");
    }
#endif

    for (;;)
    {
        kernel_idle_poll();
        cpu_idle();
    }
}
