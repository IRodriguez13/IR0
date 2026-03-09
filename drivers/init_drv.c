/**
 * IR0 Kernel — Driver Initialization
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: drivers/init_drv.c
 * Description: Multi-language driver initialization and hardware driver registry
 */

#include "init_drv.h"
#include <ir0/driver.h>
#include <ir0/logging.h>
#include <config.h>
#include <drivers/multilang_drivers.h>
#include "bluetooth/bluetooth_init.h"
#include <drivers/IO/ps2.h>
#include <interrupt/arch/keyboard.h>
#include <interrupt/arch/pic.h>
#include <drivers/IO/ps2_mouse.h>
#include <drivers/IO/pc_speaker.h>
#include <drivers/audio/sound_blaster.h>
#include <drivers/audio/adlib.h>
#include <drivers/storage/ata.h>
#include <drivers/storage/block_dev.h>
#include <ir0/net.h>
#include <drivers/dma/dma.h>
#include <drivers/serial/serial.h>
#include <kernel/resource_registry.h>
#include <kernel/driver_layer.h>

/**
 * Initialize multi-language driver subsystem
 * Called during kernel boot after heap is initialized
 */
void drivers_init(void)
{
    /* Initialize driver registry */
    ir0_driver_registry_init();
    log_subsystem_ok("DRIVER_REGISTRY");

    /* Register Bluetooth subsystem */
    if (bluetooth_register_driver() == 0) {
        LOG_INFO("KERNEL", "Bluetooth subsystem registered successfully");
    } else {
        LOG_WARNING("KERNEL", "Bluetooth subsystem registration failed");
    }

    /* Register multi-language example drivers (optional, for testing) */
#if KERNEL_ENABLE_EXAMPLE_DRIVERS
    register_multilang_example_drivers();
    log_subsystem_ok("MULTI_LANG_DRIVERS");
#endif

    /*
     * Registrar callbacks de init en la capa unificada.
     * El kernel invoca driver_layer_run_all_inits() sin conocer drivers concretos.
     */
    driver_layer_register_init("ps2_keyboard", (driver_init_cb_t)ps2_init);
    driver_layer_register_init("keyboard", (driver_init_cb_t)keyboard_init);
    driver_layer_register_init("ps2_mouse", (driver_init_cb_t)ps2_mouse_init);
    driver_layer_register_init("pc_speaker", (driver_init_cb_t)pc_speaker_init);
    driver_layer_register_init("sb16", (driver_init_cb_t)sb16_init);
    driver_layer_register_init("adlib", (driver_init_cb_t)adlib_init);
    driver_layer_register_init("ata", (driver_init_cb_t)ata_init);
    driver_layer_register_init("ata_block", (driver_init_cb_t)ata_block_register);
    driver_layer_register_init("net", (driver_init_cb_t)init_net_stack);
}


/**
 * init_all_drivers - Inicializa todos los drivers vía capa de callbacks
 *
 * No conoce drivers concretos; solo invoca los callbacks registrados.
 * IRQ y recursos se registran tras los inits.
 */
void init_all_drivers(void)
{
    serial_print("[DRIVERS] Initializing all hardware drivers (callback layer)...\n");

    driver_layer_run_all_inits();

    pic_unmask_irq(1);
    log_subsystem_ok("PS2_KEYBOARD");
    pic_unmask_irq(11);
    log_subsystem_ok("NETWORK_STACK");
    resource_register_ioport(DMA1_PORT_START, DMA1_PORT_END, "dma1");

    serial_print("[DRIVERS] All drivers initialized successfully\n");
}

