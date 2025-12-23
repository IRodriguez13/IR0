/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025 Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: adlib.c
 * Description: Adlib (Yamaha YM3812 OPL2) audio driver implementation
 */

#include "adlib.h"
#include <interrupt/arch/io.h>
#include <ir0/driver.h>
#include <ir0/logging.h>
#include <stdbool.h>

/* Adlib state */
static bool adlib_initialized = false;

/* Forward declarations */
static int32_t adlib_hw_init(void);

/* Driver registration structures */
static ir0_driver_ops_t adlib_ops = {
    .init = adlib_hw_init,
    .shutdown = adlib_shutdown
};

static ir0_driver_info_t adlib_info = {
    .name = "Adlib OPL2",
    .version = "1.0",
    .author = "Iván Rodriguez",
    .description = "ISA Adlib (Yamaha YM3812 OPL2) FM Synthesis Audio Driver",
    .language = IR0_DRIVER_LANG_C
};

/**
 * adlib_write - Write to an OPL2 register
 * @reg: Register address (0x01-0xFF)
 * @value: Value to write
 */
void adlib_write(uint8_t reg, uint8_t value)
{
    /* Write register address */
    outb(ADLIB_ADDRESS_PORT, reg);
    
    /* Small delay (OPL2 needs time to latch address) */
    for (volatile int i = 0; i < 6; i++);
    
    /* Write data */
    outb(ADLIB_DATA_PORT, value);
    
    /* Wait for write to complete (OPL2 needs time to process) */
    for (volatile int i = 0; i < 35; i++);
}

/**
 * adlib_read - Read from an OPL2 register
 * @reg: Register address (0x01-0xFF)
 * @return: Register value
 * 
 * Note: OPL2 has very limited readable registers. Most are write-only.
 * This function is mainly for detection.
 */
uint8_t adlib_read(uint8_t reg)
{
    /* Write register address */
    outb(ADLIB_ADDRESS_PORT, reg);
    
    /* Small delay */
    for (volatile int i = 0; i < 6; i++);
    
    /* Read from status register (register 4 when address = 4) */
    if (reg == ADLIB_REG_TIMER_CTRL)
        return inb(ADLIB_ADDRESS_PORT);
    
    
    return 0;
}

/**
 * adlib_detect - Detect if Adlib card is present
 * @return: true if Adlib is detected, false otherwise
 */
bool adlib_detect(void)
{
    uint8_t status1, status2;
    
    /* Reset both timers */
    adlib_write(ADLIB_REG_TIMER_CTRL, 0x60);
    adlib_write(ADLIB_REG_TIMER1, 0xFF);
    adlib_write(ADLIB_REG_TIMER2, 0xFF);
    
    /* Start timer 1 */
    adlib_write(ADLIB_REG_TIMER_CTRL, 0x21);
    
    /* Small delay */
    for (volatile int i = 0; i < 100; i++);
    
    /* Read status */
    status1 = adlib_read(ADLIB_REG_TIMER_CTRL);
    
    /* Start timer 2 */
    adlib_write(ADLIB_REG_TIMER_CTRL, 0x60);
    adlib_write(ADLIB_REG_TIMER2, 0xFF);
    adlib_write(ADLIB_REG_TIMER_CTRL, 0x40);
    
    /* Small delay */
    for (volatile int i = 0; i < 100; i++);
    
    /* Read status */
    status2 = adlib_read(ADLIB_REG_TIMER_CTRL);
    
    /* Reset timers */
    adlib_write(ADLIB_REG_TIMER_CTRL, 0x60);
    
    /* Check if timers are working (bits 7 and 6 should change) */
    if ((status1 & 0xE0) != (status2 & 0xE0))
    {
        return true;
    }
    
    return false;
}

/**
 * adlib_init - Register Adlib driver
 */
bool adlib_init(void)
{
    LOG_INFO("Adlib", "Registering Adlib OPL2 driver...");
    ir0_register_driver(&adlib_info, &adlib_ops);
    return true;
}

/**
 * adlib_hw_init - Initialize Adlib hardware
 */
static int32_t adlib_hw_init(void)
{
    LOG_INFO("Adlib", "Initializing Adlib OPL2 hardware...");
    
    /* Detect Adlib card */
    if (!adlib_detect())
    {
        LOG_WARNING("Adlib", "Adlib card not detected (this is normal if no hardware is present)");
        return 0; /* Not an error, just no hardware */
    }
    
    LOG_INFO("Adlib", "Adlib OPL2 card detected");
    
    /* Reset the chip */
    adlib_write(ADLIB_REG_TIMER_CTRL, 0x60);
    
    /* Initialize to a known state */
    adlib_write(ADLIB_REG_FM_MODE, 0x00);
    
    adlib_initialized = true;
    LOG_INFO("Adlib", "Adlib OPL2 initialized successfully");
    
    return 0;
}

/**
 * adlib_shutdown - Shutdown Adlib driver
 */
void adlib_shutdown(void)
{
    if (!adlib_initialized)
    {
        return;
    }
    
    /* Reset timers */
    adlib_write(ADLIB_REG_TIMER_CTRL, 0x60);
    
    /* Turn off all channels (would need to iterate through all channels) */
    
    adlib_initialized = false;
    LOG_INFO("Adlib", "Adlib OPL2 shutdown");
}

/**
 * adlib_is_available - Check if Adlib is available
 * @return: true if initialized and available, false otherwise
 */
bool adlib_is_available(void)
{
    return adlib_initialized;
}

