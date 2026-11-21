/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: boot_init.c
 * Description: Boot steps (logging, smokes, optional parts); kmain() in
 *              kernel/main.c only sequences these calls.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/vga.h>
#include <ir0/oops.h>
#include <ir0/cmdline.h>
#include <ir0/boot_log_hostshare.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <ir0/kmem.h>
#include <ir0/pmm.h>
#include <ir0/paging.h>
#include <ir0/cpu.h>
#include <ir0/irq.h>
#include <asm/setup.h>
#include <config.h>
#include <ir0/version.h>
#include <ir0/elf_loader.h>
#include <ir0/clock.h>
#include <ir0/init_drv.h>
#include <ir0/blockdev.h>
#include <ir0/audio_backend.h>
#include <ir0/video_backend.h>
#include <ir0/console_backend.h>
#include <ir0/console.h>
#include <ir0/ktm/ktm.h>
#include <ir0/ktm/klog.h>
#include <ir0/boot_arch.h>
#include <ir0/boot_log.h>
#include <ir0/boot_memory.h>
#include <ir0/boot_firmware_sysfs.h>
#include <ir0/init_handoff.h>
#include <ir0/vfs.h>
#include <ir0/swap.h>
#include <ir0/ipc.h>
#include <kernel/syscalls/syscalls.h>
#include <ir0/sched.h>
#include <ir0/process.h>
#include <ir0/idle.h>
#include <ir0/process_exit.h>
#include <ir0/rootfs_base.h>
#include <ir0/boot_params.h>
#include <ir0/boot_port.h>
#include <ir0/direct_map.h>
#include "boot_init.h"

void boot_firmware_init(uintptr_t firmware_handle)
{
	struct ir0_boot_params handoff = { 0 };
	const struct ir0_boot_params *params;

	ir0_boot_port_params(firmware_handle, &handoff);
	set_boot_params(&handoff);
	params = get_boot_params();

	ir0_boot_arch_firmware_early(params);
	(void)boot_memory_snapshot_init(params);
	/* Every later phys_to_virt() relies on it (frames, roots, tables). */
	if (direct_map_init() != 0)
		panic("direct map: boot RAM inventory unusable");
	ir0_boot_firmware_sysfs_init();
	ir0_cmdline_apply_log_profile();
}

void boot_console_init(void)
{
	early_init();
	heap_init();

#if CONFIG_ENABLE_VBE
	if (video_backend_init_from_boot() != 0)
		video_backend_init_fallback();
#endif
	console_backend_init();
	if (console_backend_uses_framebuffer())
		console_backend_clear(0x0F);
}

void boot_memory_init(void)
{
	/*
	 * The architecture facade supplies reserved-free RAM banks, including
	 * memory above the old 512 MiB window. Fragmented targets use the same
	 * generic PMM path.
	 */
	if (boot_memory_init_pmm() != 0)
		panic("PMM: architecture boot-memory inventory unavailable");

	/*
	 * Pin the boot address-space root while still on the identity map from the
	 * assembly bootstrap; process roots copy its kernel half.
	 */
	paging_pin_kernel_address_space(paging_current_address_space());
}

void boot_log_init(void)
{
	ir0_boot_port_serial_before_logging();
	(void)klog_promote_normal_ring();
}

void boot_serial_init(void)
{
	ir0_boot_port_serial_after_driver_registry();

	klog_set_boot_phase(KLOG_BOOT_EARLY_ARCH);
	ir0_boot_serial_ready();
	ir0_boot_arch(boot_entry_name());
	ir0_boot_info("SMP", "UP (1 CPU online)");

	klog_set_boot_phase(KLOG_BOOT_MEMORY);
	klog_info("PMM", "physical memory manager online");
	if (boot_memory_check_pmm() == 0)
		ir0_boot_smoke("PMM_RESERVED_RANGES_OK");
	else
		ir0_boot_smoke("PMM_RESERVED_RANGES_OVERLAP");
	klog_set_boot_phase(KLOG_BOOT_PLATFORM);
	ir0_boot_log_platform_hypervisor();

#if CONFIG_ENABLE_VBE
	{
		uint32_t w = 0, h = 0, bpp = 0;

		if (console_backend_uses_framebuffer() &&
		    video_backend_get_info(&w, &h, &bpp))
		{
			klog_info_fmt("BOOT", "Console: framebuffer %ux%ux%u",
				      (unsigned)w, (unsigned)h, (unsigned)bpp);
		}
		else
		{
#if DEBUG_BOOT
			klog_info_fmt(
				"BOOT",
				"Console: VGA text (80x25)%s vbe_fail_reason=%u",
				video_backend_is_available()
					? " [vbe fallback - may not be visible in graphics mode]"
					: "",
				(unsigned)video_backend_fail_reason());

			if (!boot_memory_snapshot())
				klog_info("BOOT", "Firmware snapshot unavailable");
#endif
		}
	}
#endif

	klog_info("CORE", "Registered and Initialized OK");
}

void boot_drivers_init(void)
{
	klog_set_boot_phase(KLOG_BOOT_DRIVERS);
	init_all_drivers();
}

void boot_rootfs_init(void)
{
	klog_set_boot_phase(KLOG_BOOT_STORAGE);
	if (!ir0_block_lookup_by_name(CONFIG_ROOT_BLOCK_DEVICE))
	{
		klog_warn("BOOT", "Configured root block device not detected");
		klog_warn("BOOT", "Filesystem initialization may fail");
	}
#if DEBUG_BOOT
	else
	{
		klog_info("BOOT",
			  "Configured root block device detected, proceeding with filesystem init");
	}
#endif

	klog_set_boot_phase(KLOG_BOOT_ROOTFS);
	vfs_init_root();
	klog_info("FILESYSTEM", "Registered and Initialized OK");

	ir0_boot_port_maybe_seed_init_from_embedded();

	/* After the port hook: the arm64 boot pivot would move it under /bpivot. */
	if (vfs_mount("heartfs", "/heart", "heartfs") != 0)
		klog_warn("BOOT", "heartfs mount at /heart failed");
	if (vfs_mount("sysfs", "/sys", "sysfs") != 0)
		klog_warn("BOOT", "sysfs mount at /sys failed");
}

void boot_process_init(void)
{
	process_init();
	ir0_swap_init();
	process_exit_bind_product();
	klog_info("PROCESSES", "Registered and Initialized OK");
}

void boot_ipc_init(void)
{
	ipc_init();
	klog_info("IPC", "Registered and Initialized OK");
}

void boot_clock_init(void)
{
	klog_set_boot_phase(KLOG_BOOT_TIME);
	clock_system_init();
	klog_info("CLOCK", "monotonic clock online");
	if (sched_selftest() == 0)
		klog_smoke("SCHED_POLICY_OK");
	else
		klog_smoke("SCHED_POLICY_FAIL");
	klog_info_fmt("SCHED", "SCHED_POLICY=%s", sched_policy_name());
}

void boot_syscalls_init(void)
{
	syscall_init();
	syscalls_init();
	klog_info("SYSCALLS", "Registered and Initialized OK");
}

void boot_irq_init(void)
{
	klog_set_boot_phase(KLOG_BOOT_INTERRUPTS);
	irq_init();
	ir0_boot_port_unmask_irqs();
	enable_interrupts();
	audio_backend_post_irq_selftest();
#if DEBUG_BOOT
	klog_info("BOOT", "Interrupts enabled globally (sti)");
#endif
	klog_info("INTERRUPTS", "Registered and Initialized OK");

	klog_set_boot_phase(KLOG_BOOT_READY);
	klog_notice("BOOT", "kernel core initialization complete");
}

void boot_diagnostics(void)
{
#if CONFIG_KTM
	ktm_core_init();
	KTM_CHECKPOINT(KTM_CP_BOOT_READY);
#if CONFIG_KTM_TEST
	ktm_scenarios_run_boot();
#endif
	klog_notice("BOOT", "KTM validation complete");
#endif

	(void)ir0_boot_log_hostshare_try();

#if defined(IR0_KERNEL_TESTS)
	{
		process_t *saved = current_process;
		process_t *holder;
		pid_t holder_pid;

		holder_pid = spawn_kernel(kernel_idle_loop, "ktest-ctx");
		holder = (holder_pid > 0) ? process_find_by_pid(holder_pid)
					 : NULL;
		if (holder)
		{
			process_set_current(holder);
			if (ktm_scenario_run("kernel.ktest") != KTM_OK)
				klog_error("KTEST", "kernel.ktest scenario failed");
			process_set_current(saved);
		}
		else
			klog_error("KTEST", "no process context; suite skipped");
	}
#endif
}

static int handoff_prepare_rootfs(void *context)
{
	(void)context;
	ir0_rootfs_prepare_userspace_base();
	return 0;
}

static int handoff_prepare_init_task(void *context)
{
	(void)context;
	process_prepare_pid1_for_init();
	return 0;
}

static long handoff_load_init(void *context, const char *path,
			      char *const argv[])
{
	(void)context;
	return kexecve(path, argv, NULL);
}

static int handoff_spawn_idle(void *context)
{
	int result;

	(void)context;
	result = spawn_kernel(kernel_idle_loop, "idle") < 0 ? -1 : 0;
	if (result != 0)
		klog_notice("BOOT",
			    "idle task spawn failed; UP idle heuristic only");
	return result;
}

static void handoff_init_loaded(void *context, long init_pid)
{
	(void)context;
	klog_info_fmt("INIT", "/sbin/init loaded (PID %d), scheduling",
		      (int)init_pid);
	klog_smoke("INIT_EXEC_LOADED_OK");
}

static void handoff_attach_console(void *context)
{
	(void)context;
	ir0_console_on_userspace_attach();
}

static void handoff_schedule(void *context)
{
	(void)context;
	schedule();
}

void boot_enter_userspace(void)
{
	long init_pid;
	char *argv_init[] = { "/sbin/init", NULL };
	static const struct ir0_init_handoff_ops ops = {
		.prepare_rootfs = handoff_prepare_rootfs,
		.prepare_init_task = handoff_prepare_init_task,
		.load_init = handoff_load_init,
		.init_loaded = handoff_init_loaded,
		.spawn_idle = handoff_spawn_idle,
		.attach_console = handoff_attach_console,
		.schedule = handoff_schedule,
	};

	klog_set_boot_phase(KLOG_BOOT_USERSPACE);
	klog_notice("BOOT", "system ready for userspace");
	ir0_boot_arch_userspace_handoff_smoke();
	klog_event(KLOG_EVENT_USERSPACE_HANDOFF, 0, KLOG_LEVEL_INFO, "INIT",
		   "exec /sbin/init");
	init_pid = ir0_init_handoff_run(&ops, NULL, "/sbin/init", argv_init);
	if (init_pid < 0)
	{
		klog_error("BOOT", "FAILED to load /sbin/init");
		panic("Failed to load /sbin/init");
	}
	panic("schedule returned after userspace init");
}
