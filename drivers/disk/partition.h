#ifndef _PARTITION_H
#define _PARTITION_H

#include <stdint.h>

// MBR Partition Entry Structure
typedef struct {
    uint8_t boot_indicator;
    uint8_t start_head;
    uint8_t start_sector;
    uint8_t start_cylinder;
    uint8_t system_id;
    uint8_t end_head;
    uint8_t end_sector;
    uint8_t end_cylinder;
    uint32_t start_lba;
    uint32_t total_sectors;
} __attribute__((packed)) mbr_partition_entry_t;

// MBR Structure
typedef struct {
    uint8_t bootstrap[446];
    mbr_partition_entry_t partitions[4];
    uint16_t signature;
} __attribute__((packed)) mbr_t;

// GPT Header
typedef struct {
    uint8_t signature[8];
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved;
    uint64_t current_lba;
    uint64_t backup_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t disk_guid[16];
    uint64_t partition_entry_lba;
    uint32_t num_partition_entries;
    uint32_t size_of_partition_entry;
    uint32_t partition_entry_array_crc32;
} __attribute__((packed)) gpt_header_t;

// GPT Partition Entry
typedef struct {
    uint8_t type_guid[16];
    uint8_t unique_guid[16];
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t attributes;
    uint16_t name[36];  // UTF-16LE
} __attribute__((packed)) gpt_partition_entry_t;

// Function declarations
int read_partition_table(uint8_t disk_id);
int is_gpt_disk(uint8_t disk_id);
const char* get_partition_type(uint8_t system_id);

#endif