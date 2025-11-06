// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ata.c
 * Description: ATA/IDE disk driver for storage device access
 */

#include "ata.h"
#include <ir0/print.h>
#include <ir0/panic/panic.h>
#include <string.h>
#include <arch/common/arch_interface.h>

// Global variables
bool ata_drives_present[4] = {false, false, false, false};

// Using I/O functions from arch_interface.h

static inline void outw(uint16_t port, uint16_t value) 
{
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) 
{
    uint16_t value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

// Get port base for drive
static uint16_t ata_get_port_base(uint8_t drive) 
{
    if (drive < 2) 
    {
        return ATA_PRIMARY_DATA;
    } 
    else 
    {
        return ATA_SECONDARY_DATA;
    }
}

/**
 * ata_get_status_port - get status port for drive
 * @drive: drive number (0-3)
 *
 * Returns status port address for the specified drive.
 */
static uint16_t ata_get_status_port(uint8_t drive)
{
	return (drive < 2) ? ATA_PRIMARY_STATUS : ATA_SECONDARY_STATUS;
}

/**
 * ata_get_drive_head_port - get drive head port for drive
 * @drive: drive number (0-3)
 *
 * Returns drive head port address for the specified drive.
 */
static uint16_t ata_get_drive_head_port(uint8_t drive)
{
	return (drive < 2) ? ATA_PRIMARY_DRIVE_HEAD : ATA_SECONDARY_DRIVE_HEAD;
}

/**
 * ata_get_sector_count_port - get sector count port for drive
 * @drive: drive number (0-3)
 *
 * Returns sector count port address for the specified drive.
 */
static uint16_t ata_get_sector_count_port(uint8_t drive)
{
	return (drive < 2) ? ATA_PRIMARY_SECTOR_COUNT : ATA_SECONDARY_SECTOR_COUNT;
}

/**
 * ata_get_lba_low_port - get LBA low port for drive
 * @drive: drive number (0-3)
 *
 * Returns LBA low port address for the specified drive.
 */
static uint16_t ata_get_lba_low_port(uint8_t drive)
{
	return (drive < 2) ? ATA_PRIMARY_LBA_LOW : ATA_SECONDARY_LBA_LOW;
}

static uint16_t ata_get_lba_mid_port(uint8_t drive) 
{
    if (drive < 2) 
    {
        return ATA_PRIMARY_LBA_MID;
    } 
    else 
    {
        return ATA_SECONDARY_LBA_MID;
    }
}

static uint16_t ata_get_lba_high_port(uint8_t drive) 
{
    if (drive < 2) 
    {
        return ATA_PRIMARY_LBA_HIGH;
    } 
    else 
    {
        return ATA_SECONDARY_LBA_HIGH;
    }
}

// Get command port for drive
static uint16_t ata_get_command_port(uint8_t drive) 
{
    if (drive < 2) 
    {
        return ATA_PRIMARY_COMMAND;
    } 
    else 
    {
        return ATA_SECONDARY_COMMAND;
    }
}

void ata_init(void) 
{
    // Reset all drives
    for (int i = 0; i < 4; i++) 
    {
        ata_reset_drive(i);
    }
    
    // Try to identify drives
    for (int i = 0; i < 4; i++) 
    {
        ata_drives_present[i] = ata_identify_drive(i);
    }
}

bool ata_is_available(void) 
{
    // Check if any drive is present
    for (int i = 0; i < 4; i++) 
    {
        if (ata_drives_present[i]) 
        {
            return true;
        }
    }
    return false;
}

void ata_reset_drive(uint8_t drive) 
{
    uint16_t status_port = ata_get_status_port(drive);
    uint16_t drive_head_port = ata_get_drive_head_port(drive);
    
    // Select drive
    uint8_t drive_select = (drive % 2 == 0) ? ATA_DRIVE_MASTER : ATA_DRIVE_SLAVE;
    outb(drive_head_port, drive_select);
    
    // Wait a bit
    for (volatile int i = 0; i < 1000; i++);
    
    // Check if drive is present
    uint8_t status = inb(status_port);
    if (status == 0xFF) 
    {
        return; // No drive
    }
    
    // Wait for drive to be ready
    ata_wait_ready(drive);
}

bool ata_identify_drive(uint8_t drive) 
{
    
    uint16_t status_port = ata_get_status_port(drive);
    (void)status_port; // Variable not used in this implementation
    uint16_t drive_head_port = ata_get_drive_head_port(drive);
    uint16_t command_port = ata_get_command_port(drive);
    uint16_t data_port = ata_get_port_base(drive);
    
    // Select drive
    uint8_t drive_select = (drive % 2 == 0) ? ATA_DRIVE_MASTER : ATA_DRIVE_SLAVE;
    outb(drive_head_port, drive_select);
    
    // Wait for drive to be ready
    if (!ata_wait_ready(drive)) 
    {
        return false;
    }
    
    // Send IDENTIFY command
    outb(command_port, ATA_CMD_IDENTIFY);
    
    // Wait for response
    if (!ata_wait_drq(drive)) 
    {
        return false;
    }
    
    // Read identify data (we don't need to store it for basic functionality)
    uint16_t identify_data[256];
    (void)identify_data; // Variable not used in this implementation
    for (int i = 0; i < 256; i++) 
    {
        identify_data[i] = inw(data_port);
    }
    
    return true;
}

bool ata_wait_ready(uint8_t drive) 
{
    uint16_t status_port = ata_get_status_port(drive);
    
    for (int i = 0; i < 10000; i++) 
    {
        uint8_t status = inb(status_port);
        
        if (!(status & ATA_STATUS_BSY)) 
        {
            return true;
        }
    }
    
    return false;
}

bool ata_wait_drq(uint8_t drive) 
{
    uint16_t status_port = ata_get_status_port(drive);
    
    for (int i = 0; i < 10000; i++) 
    {
        uint8_t status = inb(status_port);
        
        if (status & ATA_STATUS_ERR) 
        {
            return false;
        }
        
        if (status & ATA_STATUS_DRQ) 
        {
            return true;
        }
    }
    
    return false;
}

bool ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t num_sectors, void* buffer) 
{
    
    if (!ata_drives_present[drive]) 
    {
        return false;
    }
    
    uint16_t status_port = ata_get_status_port(drive);
    (void)status_port; // Variable not used in this implementation
    uint16_t drive_head_port = ata_get_drive_head_port(drive);
    uint16_t sector_count_port = ata_get_sector_count_port(drive);
    uint16_t lba_low_port = ata_get_lba_low_port(drive);
    uint16_t lba_mid_port = ata_get_lba_mid_port(drive);
    uint16_t lba_high_port = ata_get_lba_high_port(drive);
    uint16_t command_port = ata_get_command_port(drive);
    uint16_t data_port = ata_get_port_base(drive);
    
    // Select drive
    uint8_t drive_select = (drive % 2 == 0) ? ATA_DRIVE_MASTER : ATA_DRIVE_SLAVE;
    outb(drive_head_port, drive_select | 0x40); // LBA mode
    
    // Wait for drive to be ready
    if (!ata_wait_ready(drive)) 
    {
        return false;
    }
    
    // Set up LBA address
    outb(sector_count_port, num_sectors);
    outb(lba_low_port, lba & 0xFF);
    outb(lba_mid_port, (lba >> 8) & 0xFF);
    outb(lba_high_port, (lba >> 16) & 0xFF);
    outb(drive_head_port, drive_select | 0x40 | ((lba >> 24) & 0x0F));
    
    // Send read command
    outb(command_port, ATA_CMD_READ_SECTORS);
    
    // Read data
    uint16_t* buffer16 = (uint16_t*)buffer;
    for (int sector = 0; sector < num_sectors; sector++) 
    {
        // Wait for data
        if (!ata_wait_drq(drive)) 
        {
            return false;
        }
        
        // Read sector
        for (int i = 0; i < 256; i++) 
        {
            buffer16[sector * 256 + i] = inw(data_port);
        }
    }
    
    return true;
}

bool ata_write_sectors(uint8_t drive, uint32_t lba, uint8_t num_sectors, const void* buffer) 
{
    
    if (!ata_drives_present[drive]) 
    {
        return false;
    }
    
    uint16_t status_port = ata_get_status_port(drive);
    (void)status_port; // Variable not used in this implementation
    uint16_t drive_head_port = ata_get_drive_head_port(drive);
    uint16_t sector_count_port = ata_get_sector_count_port(drive);
    uint16_t lba_low_port = ata_get_lba_low_port(drive);
    uint16_t lba_mid_port = ata_get_lba_mid_port(drive);
    uint16_t lba_high_port = ata_get_lba_high_port(drive);
    uint16_t command_port = ata_get_command_port(drive);
    uint16_t data_port = ata_get_port_base(drive);
    
    // Select drive
    uint8_t drive_select = (drive % 2 == 0) ? ATA_DRIVE_MASTER : ATA_DRIVE_SLAVE;
    outb(drive_head_port, drive_select | 0x40); // LBA mode
    
    // Wait for drive to be ready
    if (!ata_wait_ready(drive)) 
    {
        return false;
    }
    
    // Set up LBA address
    outb(sector_count_port, num_sectors);
    outb(lba_low_port, lba & 0xFF);
    outb(lba_mid_port, (lba >> 8) & 0xFF);
    outb(lba_high_port, (lba >> 16) & 0xFF);
    outb(drive_head_port, drive_select | 0x40 | ((lba >> 24) & 0x0F));
    
    // Send write command
    outb(command_port, ATA_CMD_WRITE_SECTORS);
    
    // Write data
    const uint16_t* buffer16 = (const uint16_t*)buffer;
    for (int sector = 0; sector < num_sectors; sector++) 
    {
        // Wait for DRQ
        if (!ata_wait_drq(drive)) 
        {
            return false;
        }
        
        // Write sector
        for (int i = 0; i < 256; i++) 
        {
            outw(data_port, buffer16[sector * 256 + i]);
        }
    }
    
    // Flush cache
    outb(command_port, ATA_CMD_FLUSH_CACHE);
    
    // Wait for completion
    bool result = ata_wait_ready(drive);

    return result;
}

