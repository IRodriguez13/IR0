#include "ata.h"
#include <string.h>

// Global variables
extern bool ata_drives_present[4];
extern ata_device_info_t ata_devices[4];

bool ata_drive_present(uint8_t drive) {
    if (drive >= 4) return false;
    return ata_drives_present[drive];
}

uint64_t ata_get_size(uint8_t drive) {
    if (drive >= 4 || !ata_drives_present[drive]) return 0;
    return ata_devices[drive].size;
}

const char* ata_get_model(uint8_t drive) {
    static const char* unknown = "UNKNOWN";
    if (drive >= 4 || !ata_drives_present[drive]) return unknown;
    return ata_devices[drive].model;
}

const char* ata_get_serial(uint8_t drive) {
    static const char* unknown = "UNKNOWN";
    if (drive >= 4 || !ata_drives_present[drive]) return unknown;
    return ata_devices[drive].serial;
}
