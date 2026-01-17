// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: partition.c
 * Description: Partition table parsing and management (MBR and GPT support)
 */

#include "partition.h"
#include <string.h>
#include <drivers/storage/ata.h>
#include <kernel/process.h>

/**
 * Global partition storage
 */
static partition_info_t partitions[MAX_TOTAL_PARTITIONS];
static uint32_t partition_count = 0;
static uint32_t partition_counts_per_disk[MAX_DISKS] = {0};

/**
 * Forward declarations
 */
static int read_gpt_header(uint8_t disk_id, gpt_header_t *header);
static int read_gpt_partitions(uint8_t disk_id, const gpt_header_t *header);
static int add_partition_info(const partition_info_t *info);

/**
 * read_partition_table - Read and parse partition table (both MBR and GPT)
 * @disk_id: Disk identifier (0-3)
 *
 * Returns 0 on success, -1 on error.
 */
int read_partition_table(uint8_t disk_id)
{
    uint8_t sector[512];

    /**
     * Read first sector (MBR or GPT Protective MBR)
     */
    if (ata_read_sectors(disk_id, 0, 1, sector) != 0)
    {
        return -1;
    }

    mbr_t *mbr = (mbr_t *)sector;

    /**
     * Check MBR signature
     */
    if (mbr->signature != 0xAA55)
    {
        return -1;
    }

    /**
     * Check if it's a GPT disk by examining the protective MBR
     */
    if (mbr->partitions[0].system_id == 0xEE)
    {
        gpt_header_t gpt_header;
        if (read_gpt_header(disk_id, &gpt_header) == 0)
        {
            return read_gpt_partitions(disk_id, &gpt_header);
        }
    }

    /**
     * Parse MBR partitions
     */
    for (int i = 0; i < 4; i++)
    {
        if (mbr->partitions[i].system_id != 0)
        {
            /**
             * Store partition info for future use
             */
            partition_info_t part_info = {0};
            part_info.disk_id = disk_id;
            part_info.partition_number = i;
            part_info.is_gpt = 0;
            part_info.system_id = mbr->partitions[i].system_id;
            part_info.start_lba = mbr->partitions[i].start_lba;
            part_info.total_sectors = mbr->partitions[i].total_sectors;
            part_info.end_lba = part_info.start_lba + part_info.total_sectors - 1;
            part_info.bootable = (mbr->partitions[i].boot_indicator == 0x80);
            
            add_partition_info(&part_info);
        }
    }

    return 0;
}

/**
 * is_gpt_disk - Check if disk uses GPT partition table
 * @disk_id: Disk identifier (0-3)
 *
 * Returns 1 if GPT, 0 if not GPT or error.
 */
int is_gpt_disk(uint8_t disk_id)
{
    uint8_t sector[512];

    if (ata_read_sectors(disk_id, 0, 1, sector) != 0)
    {
        return 0;
    }

    mbr_t *mbr = (mbr_t *)sector;

    /**
     * Check MBR signature and protective partition
     */
    if (mbr->signature != 0xAA55 || mbr->partitions[0].system_id != 0xEE)
    {
        return 0;
    }

    /**
     * Verify GPT header
     */
    gpt_header_t gpt_header;
    if (read_gpt_header(disk_id, &gpt_header) != 0)
    {
        return 0;
    }

    return 1;
}

/**
 * get_partition_type - Get partition type string from system ID
 * @system_id: MBR partition system ID byte
 *
 * Returns human-readable partition type string.
 */
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

/**
 * read_gpt_header - Read GPT header from disk
 * @disk_id: Disk identifier (0-3)
 * @header: Pointer to GPT header structure to fill
 *
 * Returns 0 on success, -1 on error.
 */
static int read_gpt_header(uint8_t disk_id, gpt_header_t *header)
{
    uint8_t sector[512];

    if (ata_read_sectors(disk_id, 1, 1, sector) != 0)
    {
        return -1;
    }

    memcpy(header, sector, sizeof(gpt_header_t));

    /**
     * Verify GPT signature "EFI PART"
     */
    const uint8_t gpt_sig[] = {0x45, 0x46, 0x49, 0x20, 0x50, 0x41, 0x52, 0x54};
    if (memcmp(header->signature, gpt_sig, 8) != 0)
    {
        return -1;
    }

    return 0;
}

/**
 * read_gpt_partitions - Read GPT partition entries from disk
 * @disk_id: Disk identifier (0-3)
 * @header: Pointer to validated GPT header
 *
 * Returns 0 on success, -1 on error.
 */
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
            /**
             * Check if partition entry is used (non-zero type GUID)
             */
            uint8_t zero_guid[16] = {0};
            if (memcmp(entries[j].type_guid, zero_guid, 16) != 0)
            {
                /**
                 * Store partition info for future use
                 */
                partition_info_t part_info = {0};
                part_info.disk_id = disk_id;
                part_info.partition_number = (i * entries_per_sector) + j;
                part_info.is_gpt = 1;
                part_info.system_id = 0;  /* Not applicable for GPT */
                part_info.start_lba = entries[j].first_lba;
                part_info.end_lba = entries[j].last_lba;
                part_info.total_sectors = entries[j].last_lba - entries[j].first_lba + 1;
                part_info.bootable = 0;  /* GPT uses attributes instead */
                memcpy(part_info.type_guid, entries[j].type_guid, 16);
                memcpy(part_info.unique_guid, entries[j].unique_guid, 16);
                
                add_partition_info(&part_info);
            }
        }
    }

    return 0;
}

/**
 * add_partition_info - Add partition info to global storage
 * @info: Pointer to partition information structure
 *
 * Returns 0 on success, -1 on error.
 */
static int add_partition_info(const partition_info_t *info)
{
    if (!info || partition_count >= MAX_TOTAL_PARTITIONS)
    {
        return -1;
    }
    
    if (info->disk_id >= MAX_DISKS)
    {
        return -1;
    }
    
    partitions[partition_count] = *info;
    partition_count++;
    partition_counts_per_disk[info->disk_id]++;
    
    return 0;
}

/**
 * get_partition_count - Get number of partitions for a specific disk
 * @disk_id: Disk identifier (0-3)
 *
 * Returns number of partitions on success, -1 on error.
 */
int get_partition_count(uint8_t disk_id)
{
    if (disk_id >= MAX_DISKS)
    {
        return -1;
    }
    
    return partition_counts_per_disk[disk_id];
}

/**
 * get_partition_info - Get partition information
 * @disk_id: Disk identifier (0-3)
 * @partition_num: Partition number
 * @info: Pointer to partition_info_t structure to fill
 *
 * Returns 0 on success, -1 on error.
 */
int get_partition_info(uint8_t disk_id, uint8_t partition_num, partition_info_t *info)
{
    if (!info || disk_id >= MAX_DISKS)
    {
        return -1;
    }
    
    /**
     * Find partition matching disk_id and partition_num
     */
    for (uint32_t i = 0; i < partition_count; i++)
    {
        if (partitions[i].disk_id == disk_id && 
            partitions[i].partition_number == partition_num)
        {
            *info = partitions[i];
            return 0;
        }
    }
    
    return -1;
}