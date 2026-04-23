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
#include <ir0/driver.h>
#include <ir0/logging.h>
#include <config.h>
#include <drivers/multilang_drivers.h>
#include <drivers/IO/ps2.h>
#include <interrupt/arch/keyboard.h>
#include <interrupt/arch/pic.h>
#include <drivers/IO/pc_speaker.h>
#include <drivers/storage/ata.h>
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

#if CONFIG_ENABLE_BLUETOOTH
#include "bluetooth/bluetooth_init.h"
#endif

#if CONFIG_ENABLE_NETWORKING
#include <ir0/net.h>
#endif

/**
 * Initialize multi-language driver subsystem.
 * Called during kernel boot after heap is initialized.
 */
void drivers_init(void)
{
    ir0_driver_registry_init();
    log_subsystem_ok("DRIVER_REGISTRY");

#if CONFIG_ENABLE_BLUETOOTH
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
}


/**
 * init_all_drivers - Inicializa todos los drivers de hardware.
 *
 * Llama cada init directamente en el orden que necesita el kernel.
 * IRQ y recursos se registran tras los inits.
 */
void init_all_drivers(void)
{
    serial_print("[DRIVERS] Initializing all hardware drivers...\n");

    ps2_init();
    keyboard_init();
#if CONFIG_ENABLE_MOUSE
    ps2_mouse_init();
#endif
    pc_speaker_init();
#if CONFIG_ENABLE_SOUND
    sb16_init();
    adlib_init();
#endif
    ata_init();
    ata_block_register();
#if CONFIG_ENABLE_NETWORKING
    init_net_stack();
#endif

    log_subsystem_ok("PS2_KEYBOARD");
#if CONFIG_ENABLE_MOUSE
    log_subsystem_ok("PS2_MOUSE");
#endif
#if CONFIG_ENABLE_NETWORKING
    log_subsystem_ok("NETWORK_STACK");
#endif
#if CONFIG_ENABLE_SOUND
    resource_register_ioport(DMA1_PORT_START, DMA1_PORT_END, "dma1");
#endif

    serial_print("[DRIVERS] All drivers initialized successfully\n");
}

