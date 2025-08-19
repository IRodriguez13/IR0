#include "ata.h"
#include "../../includes/ir0/print.h"
#include "../../includes/ir0/panic/panic.h"
#include <string.h>

// Global variables
bool ata_drives_present[4] = {false, false, false, false};

// I/O functions
static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outw(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

// Get port base for drive
static uint16_t ata_get_port_base(uint8_t drive) {
    if (drive < 2) {
        return ATA_PRIMARY_DATA;
    } else {
        return ATA_SECONDARY_DATA;
    }
}

// Get status port for drive
static uint16_t ata_get_status_port(uint8_t drive) {
    if (drive < 2) {
        return ATA_PRIMARY_STATUS;
    } else {
        return ATA_SECONDARY_STATUS;
    }
}

// Get drive head port for drive
static uint16_t ata_get_drive_head_port(uint8_t drive) {
    if (drive < 2) {
        return ATA_PRIMARY_DRIVE_HEAD;
    } else {
        return ATA_SECONDARY_DRIVE_HEAD;
    }
}

// Get sector count port for drive
static uint16_t ata_get_sector_count_port(uint8_t drive) {
    if (drive < 2) {
        return ATA_PRIMARY_SECTOR_COUNT;
    } else {
        return ATA_SECONDARY_SECTOR_COUNT;
    }
}

// Get LBA ports for drive
static uint16_t ata_get_lba_low_port(uint8_t drive) {
    if (drive < 2) {
        return ATA_PRIMARY_LBA_LOW;
    } else {
        return ATA_SECONDARY_LBA_LOW;
    }
}

static uint16_t ata_get_lba_mid_port(uint8_t drive) {
    if (drive < 2) {
        return ATA_PRIMARY_LBA_MID;
    } else {
        return ATA_SECONDARY_LBA_MID;
    }
}

static uint16_t ata_get_lba_high_port(uint8_t drive) {
    if (drive < 2) {
        return ATA_PRIMARY_LBA_HIGH;
    } else {
        return ATA_SECONDARY_LBA_HIGH;
    }
}

// Get command port for drive
static uint16_t ata_get_command_port(uint8_t drive) {
    if (drive < 2) {
        return ATA_PRIMARY_COMMAND;
    } else {
        return ATA_SECONDARY_COMMAND;
    }
}

void ata_init(void) {
    print("Initializing ATA controller...\n");
    
    // Reset all drives
    for (int i = 0; i < 4; i++) {
        ata_reset_drive(i);
        ata_drives_present[i] = false;
    }
    
    // Try to identify drives
    for (int i = 0; i < 4; i++) 
    {
        if (ata_identify_drive(i)) {
            ata_drives_present[i] = true;
            print("ATA drive ");
            print_hex64(i);
            print(" detected\n");
        }
    }
    
    print("ATA controller initialized\n");
}

void ata_reset_drive(uint8_t drive) {
    uint16_t status_port = ata_get_status_port(drive);
    uint16_t drive_head_port = ata_get_drive_head_port(drive);
    
    // Select drive
    uint8_t drive_select = (drive % 2 == 0) ? ATA_DRIVE_MASTER : ATA_DRIVE_SLAVE;
    outb(drive_head_port, drive_select);
    
    // Wait a bit
    for (volatile int i = 0; i < 1000; i++);
    
    // Check if drive is present
    uint8_t status = inb(status_port);
    if (status == 0xFF) {
        return; // No drive
    }
    
    // Wait for drive to be ready
    ata_wait_ready(drive);
}

bool ata_identify_drive(uint8_t drive) {
    uint16_t status_port = ata_get_status_port(drive);
    uint16_t drive_head_port = ata_get_drive_head_port(drive);
    uint16_t command_port = ata_get_command_port(drive);
    uint16_t data_port = ata_get_port_base(drive);
    
    // Select drive
    uint8_t drive_select = (drive % 2 == 0) ? ATA_DRIVE_MASTER : ATA_DRIVE_SLAVE;
    outb(drive_head_port, drive_select);
    
    // Wait for drive to be ready
    if (!ata_wait_ready(drive)) {
        return false;
    }
    
    // Send IDENTIFY command
    outb(command_port, ATA_CMD_IDENTIFY);
    
    // Wait for response
    if (!ata_wait_drq(drive)) {
        return false;
    }
    
    // Read identify data (we don't need to store it for basic functionality)
    uint16_t identify_data[256];
    for (int i = 0; i < 256; i++) {
        identify_data[i] = inw(data_port);
    }
    
    return true;
}

bool ata_wait_ready(uint8_t drive) 
{
    uint16_t status_port = ata_get_status_port(drive);
    
    for (int i = 0; i < 10000; i++) {
        uint8_t status = inb(status_port);
        
        if (!(status & ATA_STATUS_BSY)) {
            return true;
        }
    }
    
    return false;
}

bool ata_wait_drq(uint8_t drive) {
    uint16_t status_port = ata_get_status_port(drive);
    
    for (int i = 0; i < 10000; i++) {
        uint8_t status = inb(status_port);
        
        if (status & ATA_STATUS_ERR) {
            return false;
        }
        
        if (status & ATA_STATUS_DRQ) {
            return true;
        }
    }
    
    return false;
}

bool ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t num_sectors, void* buffer) {
    if (!ata_drives_present[drive]) {
        return false;
    }
    
    uint16_t status_port = ata_get_status_port(drive);
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
    if (!ata_wait_ready(drive)) {
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
    for (int sector = 0; sector < num_sectors; sector++) {
        // Wait for data
        if (!ata_wait_drq(drive)) {
            return false;
        }
        
        // Read sector
        for (int i = 0; i < 256; i++) {
            buffer16[sector * 256 + i] = inw(data_port);
        }
    }
    
    return true;
}

bool ata_write_sectors(uint8_t drive, uint32_t lba, uint8_t num_sectors, const void* buffer) {
    if (!ata_drives_present[drive]) 
    {
        return false;
    }
    
    uint16_t status_port = ata_get_status_port(drive);
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
    if (!ata_wait_ready(drive)) {
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
    for (int sector = 0; sector < num_sectors; sector++) {
        // Wait for DRQ
        if (!ata_wait_drq(drive)) {
            return false;
        }
        
        // Write sector
        for (int i = 0; i < 256; i++) {
            outw(data_port, buffer16[sector * 256 + i]);
        }
    }
    
    // Flush cache
    outb(command_port, ATA_CMD_FLUSH_CACHE);
    
    // Wait for completion
    return ata_wait_ready(drive);
}
