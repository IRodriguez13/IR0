/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ps2.c
 * Description: PS/2 Controller driver
 */

#include "ps2.h"
#include <ir0/vga.h>
#include <ir0/oops.h>
#include <string.h>
#include <arch/common/arch_interface.h>
#include <ir0/driver.h>
#include <ir0/logging.h>

/* Internal hardware initialization function */
static int32_t ps2_hw_init(void)
{
    uint8_t config;

    LOG_INFO("PS2", "Running PS/2 Controller HW Init...");

    /* Disable both ports */
    outb(PS2_COMMAND_PORT, PS2_CMD_DISABLE_PORT1);
    outb(PS2_COMMAND_PORT, PS2_CMD_DISABLE_PORT2);
    
    /* Flush output buffer */
    inb(PS2_DATA_PORT);
    
    /* Set configuration byte */
    outb(PS2_COMMAND_PORT, PS2_CMD_READ_CONFIG);
    config = inb(PS2_DATA_PORT);
    config &= ~(PS2_CFG_INT1 | PS2_CFG_INT2); /* Disable interrupts for now */
    config |= PS2_CFG_TRANS;                  /* Enable translation */
    outb(PS2_COMMAND_PORT, PS2_CMD_WRITE_CONFIG);
    outb(PS2_DATA_PORT, config);
    
    /* Test controller */
    outb(PS2_COMMAND_PORT, PS2_CMD_TEST_CONTROLLER);
    if (inb(PS2_DATA_PORT) != PS2_RESP_SELF_TEST_OK) 
    {
        return -1; /* PS/2 controller test failed */
    }
    
    /* Enable port 1 */
    outb(PS2_COMMAND_PORT, PS2_CMD_ENABLE_PORT1);
    
    /* Test port 1 */
    outb(PS2_COMMAND_PORT, PS2_CMD_TEST_PORT1);
    if (inb(PS2_DATA_PORT) != PS2_RESP_PORT_TEST_OK) 
    {
        return -1; /* PS/2 port 1 test failed */
    }
    
    /* Set configuration byte again to enable IRQ1 */
    outb(PS2_COMMAND_PORT, PS2_CMD_READ_CONFIG);
    config = inb(PS2_DATA_PORT);
    config |= PS2_CFG_INT1;  /* Enable IRQ1 */
    outb(PS2_COMMAND_PORT, PS2_CMD_WRITE_CONFIG);
    outb(PS2_DATA_PORT, config);
    
    return 0;
}

/* Driver registration structures */
static ir0_driver_ops_t ps2_ops = {
    .init = ps2_hw_init,
    .shutdown = NULL
};

static ir0_driver_info_t ps2_info = {
    .name = "PS/2 Controller",
    .version = "1.0",
    .author = "Iván Rodriguez",
    .description = "Standard i8042 PS/2 Controller Driver",
    .language = IR0_DRIVER_LANG_C
};

/**
 * ps2_init - register PS/2 controller
 */
void ps2_init(void) 
{
    LOG_INFO("PS2", "Registering PS/2 Controller driver...");
    ir0_register_driver(&ps2_info, &ps2_ops);
}

bool ps2_send_command(uint8_t command) 
{
    int timeout = 100000;
    while (timeout--) 
    {
        if ((inb(PS2_STATUS_PORT) & 2) == 0) 
        {
            outb(PS2_DATA_PORT, command);
            return true;
        }
    }
    return false;
}

uint8_t ps2_read_data(void) 
{
    int timeout = 100000;
    while (timeout--) 
    {
        if (inb(PS2_STATUS_PORT) & 1) 
        {
            return inb(PS2_DATA_PORT);
        }
    }
    return 0;
}
