#include <drivers/timer/acpi/acpi.h>
#include <stddef.h>
#include <stdint.h>


#define ACPI_RSDP_SIGNATURE "RSD PTR "
#define ACPI_HPET_SIGNATURE 0x54455048 
#define RSDP_SEARCH_START 0x000E0000
#define RSDP_SEARCH_END   0x000FFFFF




