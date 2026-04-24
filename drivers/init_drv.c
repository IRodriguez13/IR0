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

#include "init_drv.h"
#include "driver_bootstrap.h"
#include <ir0/driver.h>
#include <ir0/logging.h>
#include <config.h>
#include <drivers/multilang_drivers.h>
#include <drivers/IO/ps2.h>
#include <interrupt/arch/keyboard.h>
#include <interrupt/arch/pic.h>
#if CONFIG_ENABLE_PC_SPEAKER
#include <drivers/IO/pc_speaker.h>
#endif
#if CONFIG_ENABLE_STORAGE_ATA
#include <drivers/storage/ata.h>
#endif
#include <drivers/storage/block_dev.h>
#include <drivers/serial/serial.h>
#include <kernel/resource_registry.h>

#if CONFIG_ENABLE_MOUSE
#include <drivers/IO/ps2_mouse.h>
#endif

#if CONFIG_ENABLE_SOUND
#include <drivers/audio/sound_blaster.h>
#include <drivers/audio/adlib.h>
#include <drivers/dma/dma.h>
#endif

#if CONFIG_ENABLE_BLUETOOTH && CONFIG_INIT_BLUETOOTH_DRIVER
#include "bluetooth/bluetooth_init.h"
#endif

#if CONFIG_ENABLE_NETWORKING
#include <ir0/net.h>
#endif

static int g_registry_ready = 0;
static int g_bootstrap_done = 0;

static int boot_init_ps2_controller(void)
{
#if CONFIG_INIT_PS2_CONTROLLER
    ps2_init();
#endif
    return 0;
}

static int boot_init_keyboard(void)
{
#if CONFIG_INIT_PS2_CONTROLLER
    keyboard_init();
#endif
    return 0;
}

static int boot_init_mouse(void)
{
#if CONFIG_ENABLE_MOUSE && CONFIG_INIT_MOUSE_DRIVER
    ps2_mouse_init();
#endif
    return 0;
}

static int boot_init_pc_speaker(void)
{
#if CONFIG_ENABLE_PC_SPEAKER && CONFIG_INIT_PC_SPEAKER
    pc_speaker_init();
#endif
    return 0;
}

static int boot_init_sound(void)
{
#if CONFIG_ENABLE_SOUND && CONFIG_INIT_SOUND_DRIVERS
    sb16_init();
    adlib_init();
    resource_register_ioport(DMA1_PORT_START, DMA1_PORT_END, "dma1");
#endif
    return 0;
}

static int boot_init_storage_ata(void)
{
#if CONFIG_ENABLE_STORAGE_ATA && CONFIG_INIT_STORAGE_ATA
    ata_init();
#endif
    return 0;
}

static int boot_init_storage_block(void)
{
#if CONFIG_ENABLE_STORAGE_ATA_BLOCK && CONFIG_INIT_STORAGE_ATA_BLOCK
    ata_block_register();
#endif
    return 0;
}

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
    driver_bootstrap_register(DRIVER_BOOT_STAGE_INPUT, "ps2_controller", boot_init_ps2_controller,
                              CONFIG_INIT_PS2_CONTROLLER);
    driver_bootstrap_register(DRIVER_BOOT_STAGE_INPUT, "ps2_keyboard", boot_init_keyboard,
                              CONFIG_INIT_PS2_CONTROLLER);
    driver_bootstrap_register(DRIVER_BOOT_STAGE_INPUT, "ps2_mouse", boot_init_mouse,
                              (CONFIG_ENABLE_MOUSE && CONFIG_INIT_MOUSE_DRIVER));
    driver_bootstrap_register(DRIVER_BOOT_STAGE_PLATFORM, "pc_speaker", boot_init_pc_speaker,
                              (CONFIG_ENABLE_PC_SPEAKER && CONFIG_INIT_PC_SPEAKER));
    driver_bootstrap_register(DRIVER_BOOT_STAGE_STORAGE, "ata_core", boot_init_storage_ata,
                              (CONFIG_ENABLE_STORAGE_ATA && CONFIG_INIT_STORAGE_ATA));
    driver_bootstrap_register(DRIVER_BOOT_STAGE_STORAGE, "ata_block", boot_init_storage_block,
                              (CONFIG_ENABLE_STORAGE_ATA_BLOCK && CONFIG_INIT_STORAGE_ATA_BLOCK));
    driver_bootstrap_register(DRIVER_BOOT_STAGE_AUDIO, "sound_stack", boot_init_sound,
                              (CONFIG_ENABLE_SOUND && CONFIG_INIT_SOUND_DRIVERS));
    driver_bootstrap_register(DRIVER_BOOT_STAGE_NETWORK, "network_stack", boot_init_network,
                              (CONFIG_ENABLE_NETWORKING && CONFIG_INIT_NETWORK_STACK));
}

/**
 * Initialize multi-language driver subsystem.
 * Called during kernel boot after heap is initialized.
 */
static void driver_registry_prepare(void)
{
    if (g_registry_ready)
        return;

    ir0_driver_registry_init();
    log_subsystem_ok("DRIVER_REGISTRY");

#if CONFIG_ENABLE_BLUETOOTH && CONFIG_INIT_BLUETOOTH_DRIVER
    if (bluetooth_register_driver() == 0) {
        LOG_INFO("KERNEL", "Bluetooth subsystem registered successfully");
    } else {
        LOG_WARNING("KERNEL", "Bluetooth subsystem registration failed");
    }
#endif

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

    serial_print("[DRIVERS] Initializing all hardware drivers...\n");
    driver_registry_prepare();
    register_bootstrap_plan();
    driver_bootstrap_run_all();

    log_subsystem_ok("PS2_KEYBOARD");
#if CONFIG_ENABLE_MOUSE && CONFIG_INIT_MOUSE_DRIVER
    log_subsystem_ok("PS2_MOUSE");
#endif
#if CONFIG_ENABLE_NETWORKING && CONFIG_INIT_NETWORK_STACK
    log_subsystem_ok("NETWORK_STACK");
#endif
    g_bootstrap_done = 1;
    serial_print("[DRIVERS] All drivers initialized successfully\n");
}

void drivers_init(void)
{
    /*
     * Compat entrypoint:
     * keep existing callers working while centralizing all init in init_all_drivers().
     */
    init_all_drivers();
}

