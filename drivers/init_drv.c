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

static const struct driver_boot_entry generic_boot_drivers[] = {
    { "network_stack", boot_init_network, DRIVER_BOOT_STAGE_NETWORK,
      CONFIG_ENABLE_NETWORKING && CONFIG_INIT_NETWORK_STACK },
};

static const struct driver_boot_table generic_boot_table =
    DRIVER_BOOT_TABLE(generic_boot_drivers);

/* Generic drivers first, then the platform (PC: i8042, ISA sound, ATA, ...). */
static const struct driver_boot_table *const boot_driver_tables[] = {
    &generic_boot_table,
    &ir0_boot_arch_driver_table,
};

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
    log_subsystem_ok("DRIVER_REGISTRY");

#if KERNEL_ENABLE_EXAMPLE_DRIVERS
    register_multilang_example_drivers();
    log_subsystem_ok("MULTI_LANG_DRIVERS");
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
    if (driver_bootstrap_run(boot_driver_tables,
                             sizeof(boot_driver_tables) /
                             sizeof(boot_driver_tables[0])) != 0)
        klog_warn("DRIVERS", "One or more selectable boot drivers failed");

    log_subsystem_ok("PS2_KEYBOARD");
#if CONFIG_ENABLE_MOUSE && CONFIG_INIT_MOUSE_DRIVER
    log_subsystem_ok("PS2_MOUSE");
#endif
#if CONFIG_ENABLE_NETWORKING && CONFIG_INIT_NETWORK_STACK
    log_subsystem_ok("NETWORK_STACK");
#endif
#if CONFIG_ENABLE_BLUETOOTH && CONFIG_INIT_BLUETOOTH_DRIVER
    log_subsystem_ok("BLUETOOTH_STACK");
#endif
#if CONFIG_ENABLE_USB_HOST && CONFIG_INIT_USB_HOST
    log_subsystem_ok("USB_HOST");
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

