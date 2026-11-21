/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: init_drv.c
 * Description: IR0 kernel source/header file
 */

/**
 * IR0 Kernel — Driver Initialization
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: drivers/init_drv.c
 * Description: Multi-language driver initialization and hardware driver init
 */

#include <ir0/init_drv.h>
#include <ir0/driver_bootstrap.h>
#include <ir0/driver.h>
#include <ir0/ktm/klog.h>
#include <string.h>
#include <config.h>
#include <drivers/multilang_drivers.h>
#include <ir0/boot_arch.h>

#if CONFIG_ENABLE_NETWORKING
#include <ir0/net.h>
#endif

static int g_registry_ready = 0;
static int g_bootstrap_done = 0;

static int boot_init_network(void)
{
#if CONFIG_ENABLE_NETWORKING && CONFIG_INIT_NETWORK_STACK
    return init_net_stack();
#else
    return 0;
#endif
}

static void register_bootstrap_plan(void)
{
    driver_bootstrap_reset();
    driver_bootstrap_register(DRIVER_BOOT_STAGE_NETWORK, "network_stack", boot_init_network,
                              (CONFIG_ENABLE_NETWORKING && CONFIG_INIT_NETWORK_STACK));
    /* Platform devices (PC: i8042, ISA sound, ATA/AHCI/NVMe, virtio-9p, ...). */
    ir0_boot_arch_drivers();
}

/**
 * Initialize multi-language driver subsystem.
 * Called during kernel boot after heap is initialized.
 */
static void driver_registry_prepare(void)
{
    if (g_registry_ready)
        return;

    if (!ir0_driver_registry_is_initialized())
        ir0_driver_registry_init();
    klog_info("DRIVER_REGISTRY", "Registered and Initialized OK");

#if KERNEL_ENABLE_EXAMPLE_DRIVERS
    register_multilang_example_drivers();
    klog_info("MULTI_LANG_DRIVERS", "Registered and Initialized OK");
#endif

    g_registry_ready = 1;
}


/**
 * init_all_drivers - Inicializa todos los drivers de hardware.
 *
 * Llama cada init directamente en el orden que necesita el kernel.
 * IRQ y recursos se registran tras los inits.
 */
void init_all_drivers(void)
{
    if (g_bootstrap_done)
        return;

    klog_info("DRIVERS", "Initializing all hardware drivers...");
    driver_registry_prepare();
    register_bootstrap_plan();
    if (driver_bootstrap_run_all() != 0)
        klog_warn("DRIVERS", "One or more selectable boot drivers failed");

    klog_info("PS2_KEYBOARD", "Registered and Initialized OK");
#if CONFIG_ENABLE_MOUSE && CONFIG_INIT_MOUSE_DRIVER
    klog_info("PS2_MOUSE", "Registered and Initialized OK");
#endif
#if CONFIG_ENABLE_NETWORKING && CONFIG_INIT_NETWORK_STACK
    klog_info("NETWORK_STACK", "Registered and Initialized OK");
#endif
#if CONFIG_ENABLE_BLUETOOTH && CONFIG_INIT_BLUETOOTH_DRIVER
    klog_info("BLUETOOTH_STACK", "Registered and Initialized OK");
#endif
#if CONFIG_ENABLE_USB_HOST && CONFIG_INIT_USB_HOST
    klog_info("USB_HOST", "Registered and Initialized OK");
#endif
    g_bootstrap_done = 1;
    {
	unsigned ready = 0, absent = 0, deferred = 0;
	unsigned unsupported = 0, failed = 0;
	char summary[96];

	ir0_driver_boot_emit_probe_results();
	ir0_driver_boot_counts(&ready, &absent, &deferred, &unsupported,
			       &failed);
	snprintf(summary, sizeof(summary),
		 "summary ready=%u absent=%u deferred=%u unsupported=%u failed=%u",
		 ready, absent, deferred, unsupported, failed);
	klog_event(KLOG_EVENT_DRIVER_SUMMARY, 0, KLOG_LEVEL_NOTICE, "DRIVERS",
		   summary);
	klog_smoke("DRIVER_SUMMARY_OK");
    }
}

