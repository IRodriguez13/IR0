#include <stdint.h>
#include "fs_types.h"

// Get filesystem type string from partition system ID
const char* get_fs_type(uint8_t system_id) {
    switch(system_id) {
        case 0x00: return "Empty";
        case 0x01: return "FAT12";
        case 0x04: return "FAT16 <32M";
        case 0x05: return "Extended";
        case 0x06: return "FAT16";
        case 0x07: return "NTFS/HPFS";
        case 0x0B: return "FAT32";
        case 0x0C: return "FAT32 LBA";
        case 0x0E: return "FAT16 LBA";
        case 0x0F: return "Extended LBA";
        case 0x82: return "Linux Swap";
        case 0x83: return "Linux";
        case 0x8E: return "Linux LVM";
        case 0xEE: return "GPT";
        default: return "Unknown";
    }
}