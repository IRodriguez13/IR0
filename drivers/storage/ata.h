#pragma once

#include <stdint.h>
#include <stdbool.h>

// ATA I/O ports
#define ATA_PRIMARY_DATA        0x1F0
#define ATA_PRIMARY_ERROR       0x1F1
#define ATA_PRIMARY_FEATURES    0x1F1
#define ATA_PRIMARY_SECTOR_COUNT 0x1F2
#define ATA_PRIMARY_LBA_LOW     0x1F3
#define ATA_PRIMARY_LBA_MID     0x1F4
#define ATA_PRIMARY_LBA_HIGH    0x1F5
#define ATA_PRIMARY_DRIVE_HEAD  0x1F6
#define ATA_PRIMARY_STATUS      0x1F7
#define ATA_PRIMARY_COMMAND     0x1F7

#define ATA_SECONDARY_DATA      0x170
#define ATA_SECONDARY_ERROR     0x171
#define ATA_SECONDARY_FEATURES  0x171
#define ATA_SECONDARY_SECTOR_COUNT 0x172
#define ATA_SECONDARY_LBA_LOW   0x173
#define ATA_SECONDARY_LBA_MID   0x174
#define ATA_SECONDARY_LBA_HIGH  0x175
#define ATA_SECONDARY_DRIVE_HEAD 0x176
#define ATA_SECONDARY_STATUS    0x177
#define ATA_SECONDARY_COMMAND   0x177

// ATA Commands
#define ATA_CMD_READ_SECTORS    0x20
#define ATA_CMD_WRITE_SECTORS   0x30
#define ATA_CMD_IDENTIFY        0xEC
#define ATA_CMD_FLUSH_CACHE     0xE7

// ATA Status register bits
#define ATA_STATUS_ERR          0x01
#define ATA_STATUS_IDX          0x02
#define ATA_STATUS_CORR         0x04
#define ATA_STATUS_DRQ          0x08
#define ATA_STATUS_SRV          0x10
#define ATA_STATUS_DF           0x20
#define ATA_STATUS_RDY          0x40
#define ATA_STATUS_BSY          0x80

// Drive selection
#define ATA_DRIVE_MASTER        0xA0
#define ATA_DRIVE_SLAVE         0xB0

// Sector size
#define ATA_SECTOR_SIZE         512

// ATA device information structure
typedef struct {
    bool present;
    bool is_atapi;
    uint64_t size;              // Size in sectors
    uint64_t capacity_bytes;    // Device capacity in bytes
    char model[41];             // NUL-terminated model string
    char serial[21];           // NUL-terminated serial number string
    uint16_t sectors_per_intr;  // Sectors per interrupt (for multi-sector transfers)
} ata_device_info_t;

// Global variables
extern bool ata_drives_present[4]; // Primary master, primary slave, secondary master, secondary slave
extern ata_device_info_t ata_devices[4];

// Core ATA functions
void ata_init(void);
bool ata_is_available(void);
bool ata_drive_present(uint8_t drive);
bool ata_identify_drive(uint8_t drive);
bool ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t num_sectors, void* buffer);
bool ata_write_sectors(uint8_t drive, uint32_t lba, uint8_t num_sectors, const void* buffer);
bool ata_wait_ready(uint8_t drive);
bool ata_wait_drq(uint8_t drive);
void ata_reset_drive(uint8_t drive);
bool ata_get_device_info(uint8_t drive, ata_device_info_t *out);

// Helper functions
uint64_t ata_get_size(uint8_t drive);
const char* ata_get_model(uint8_t drive);
const char* ata_get_serial(uint8_t drive);


