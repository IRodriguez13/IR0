#include "partition.h"
#include <string.h>
#include <drivers/storage/ata.h>
#include <kernel/process.h>

// Forward declarations
static int read_gpt_header(uint8_t disk_id, gpt_header_t *header);
static int read_gpt_partitions(uint8_t disk_id, const gpt_header_t *header);

// Read and parse partition table (both MBR and GPT)
int read_partition_table(uint8_t disk_id)
{
    uint8_t sector[512];

    // Read first sector (MBR or GPT Protective MBR)
    if (ata_read_sectors(disk_id, 0, 1, sector) != 0)
    {
        return -1;
    }

    mbr_t *mbr = (mbr_t *)sector;

    // Check MBR signature
    if (mbr->signature != 0xAA55)
    {
        return -1;
    }

    // Check if it's a GPT disk by examining the protective MBR
    if (mbr->partitions[0].system_id == 0xEE)
    {
        gpt_header_t gpt_header;
        if (read_gpt_header(disk_id, &gpt_header) == 0)
        {
            return read_gpt_partitions(disk_id, &gpt_header);
        }
    }

    // Parse MBR partitions
    for (int i = 0; i < 4; i++)
    {
        if (mbr->partitions[i].system_id != 0)
        {
            // Store partition info for future use
            // TODO: Add partition info to global structure
        }
    }

    return 0;
}

// Check if disk uses GPT
int is_gpt_disk(uint8_t disk_id)
{
    uint8_t sector[512];

    if (ata_read_sectors(disk_id, 0, 1, sector) != 0)
    {
        return 0;
    }

    mbr_t *mbr = (mbr_t *)sector;

    // Check MBR signature and protective partition
    if (mbr->signature != 0xAA55 || mbr->partitions[0].system_id != 0xEE)
    {
        return 0;
    }

    // Verify GPT header
    gpt_header_t gpt_header;
    if (read_gpt_header(disk_id, &gpt_header) != 0)
    {
        return 0;
    }

    return 1;
}

// Get partition type string from system ID
const char *get_partition_type(uint8_t system_id)
{
    switch (system_id)
    {
    case 0x00:
        return "Empty";
    case 0x01:
        return "FAT12";
    case 0x04:
        return "FAT16 <32M";
    case 0x05:
        return "Extended";
    case 0x06:
        return "FAT16";
    case 0x07:
        return "NTFS/HPFS";
    case 0x0B:
        return "FAT32";
    case 0x0C:
        return "FAT32 LBA";
    case 0x0E:
        return "FAT16 LBA";
    case 0x0F:
        return "Extended LBA";
    case 0x82:
        return "Linux Swap";
    case 0x83:
        return "Linux";
    case 0x8E:
        return "Linux LVM";
    case 0xEE:
        return "GPT";
    default:
        return "Unknown";
    }
}

// Read GPT header from disk
static int read_gpt_header(uint8_t disk_id, gpt_header_t *header)
{
    uint8_t sector[512];

    if (ata_read_sectors(disk_id, 1, 1, sector) != 0)
    {
        return -1;
    }

    memcpy(header, sector, sizeof(gpt_header_t));

    // Verify GPT signature "EFI PART"
    const uint8_t gpt_sig[] = {0x45, 0x46, 0x49, 0x20, 0x50, 0x41, 0x52, 0x54};
    if (memcmp(header->signature, gpt_sig, 8) != 0)
    {
        return -1;
    }

    return 0;
}

// Read GPT partition entries
static int read_gpt_partitions(uint8_t disk_id, const gpt_header_t *header)
{
    uint8_t buffer[512];
    uint32_t entries_per_sector = 512 / header->size_of_partition_entry;
    uint32_t sectors_to_read = (header->num_partition_entries + entries_per_sector - 1) / entries_per_sector;

    for (uint32_t i = 0; i < sectors_to_read; i++)
    {
        if (ata_read_sectors(disk_id, header->partition_entry_lba + i, 1, buffer) != 0)
        {
            return -1;
        }

        gpt_partition_entry_t *entries = (gpt_partition_entry_t *)buffer;
        uint32_t entries_this_sector = entries_per_sector;

        if (i == sectors_to_read - 1)
        {
            entries_this_sector = header->num_partition_entries % entries_per_sector;
            if (entries_this_sector == 0)
            {
                entries_this_sector = entries_per_sector;
            }
        }

        for (uint32_t j = 0; j < entries_this_sector; j++)
        {
            // Check if partition entry is used (non-zero type GUID)
            uint8_t zero_guid[16] = {0};
            if (memcmp(entries[j].type_guid, zero_guid, 16) != 0)
            {
                // Store partition info for future use
                // TODO: Add partition info to global structure
            }
        }
    }

    return 0;
}