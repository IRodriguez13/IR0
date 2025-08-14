// memory/unified_memory_layout.h - Coordinación entre paginación estática y virtual

#pragma once
#include <stdint.h>
#include <string.h>

/*
 Este es el famoso grafiquito de memoria que se ve cuando se habla del memory layout.
 Defino las políticas de uso de memoria del kernel y la correpondiente al user space.
*/

// ===============================================================================
// LAYOUT DE MEMORIA UNIFICADO PARA IR0 KERNEL
// ===============================================================================

// Memoria física
#define PHYS_MEM_START 0x02800000 // ✅ Después del identity mapping (40MB)
#define PHYS_MEM_END   0x08000000 // Hasta 128MB

// Memoria virtual - Layout coordinado
#define KERNEL_VIRT_BASE 0x00000000 // 0-64MB: Kernel code/data
#define KERNEL_VIRT_END 0x04000000

#define KERNEL_HEAP_BASE 0x04000000 // 64MB-96MB: Kernel heap (kmalloc)
#define KERNEL_HEAP_END 0x06000000

#define KERNEL_STACK_BASE 0x06000000 // 96MB-100MB: Kernel stacks
#define KERNEL_STACK_END 0x06400000

#define VMALLOC_BASE 0x10000000 // 256MB+: Virtual malloc area
#define VMALLOC_END 0x20000000  // hasta 512MB

#define USER_SPACE_BASE 0x40000000 // 1GB+: User space (futuro)
#define USER_SPACE_END 0x80000000  // hasta 2GB

// ===============================================================================
// ZONA MANAGEMENT - Evitar colisiones entre sistemas
// ===============================================================================

typedef enum
{
    ZONE_KERNEL_STATIC, // Paginación estática (0-64MB)
    ZONE_KERNEL_HEAP,   // Heap del kernel (64-96MB)
    ZONE_KERNEL_STACK,  // Stacks del kernel
    ZONE_VMALLOC,       // Virtual malloc on-demand
    ZONE_USER_SPACE,    // Futuro: procesos de usuario
    ZONE_INVALID
} memory_zone_t;

// Funciones para validar rangos
memory_zone_t get_memory_zone(uintptr_t virt_addr);
int is_zone_compatible(memory_zone_t zone, uint32_t flags);
int validate_memory_request(uintptr_t start, size_t size, memory_zone_t expected_zone);

// ===============================================================================
// COORDINACIÓN ENTRE SISTEMAS
// ===============================================================================

typedef struct
{
    uintptr_t start;
    uintptr_t end;
    memory_zone_t zone;
    uint32_t flags;
    int is_static;   // True si usa paginación estática
    int is_ondemand; // True si usa on-demand
} memory_region_t;

// Registry global de regiones de memoria
void memory_region_register(uintptr_t start, uintptr_t end, memory_zone_t zone,
                            uint32_t flags, int is_static, int is_ondemand);
memory_region_t *memory_region_find(uintptr_t virt_addr);
int memory_region_conflicts(uintptr_t start, uintptr_t end);

// ===============================================================================
// IMPLEMENTACIÓN
// ===============================================================================

#ifdef MEMORY_LAYOUT_IMPLEMENTATION

static memory_region_t memory_regions[32];
static int region_count = 0;

memory_zone_t get_memory_zone(uintptr_t virt_addr)
{
    if (virt_addr >= KERNEL_VIRT_BASE && virt_addr < KERNEL_VIRT_END)
        return ZONE_KERNEL_STATIC;
    if (virt_addr >= KERNEL_HEAP_BASE && virt_addr < KERNEL_HEAP_END)
        return ZONE_KERNEL_HEAP;
    if (virt_addr >= KERNEL_STACK_BASE && virt_addr < KERNEL_STACK_END)
        return ZONE_KERNEL_STACK;
    if (virt_addr >= VMALLOC_BASE && virt_addr < VMALLOC_END)
        return ZONE_VMALLOC;
    if (virt_addr >= USER_SPACE_BASE && virt_addr < USER_SPACE_END)
        return ZONE_USER_SPACE;

    return ZONE_INVALID;
}

void memory_region_register(uintptr_t start, uintptr_t end, memory_zone_t zone,
                            uint32_t flags, int is_static, int is_ondemand)
{
    if (region_count >= 32)
    {
        print_error("[X] Memory region registry full!\n");
        return;
    }

    // Verificar conflictos
    if (memory_region_conflicts(start, end))
    {
        print_error("[X] Memory region conflict detected!\n");
        return;
    }

    memory_regions[region_count] = (memory_region_t){
        .start = start,
        .end = end,
        .zone = zone,
        .flags = flags,
        .is_static = is_static,
        .is_ondemand = is_ondemand};

    region_count++;

    print_success("[OK] Memory region registered: 0x");
    print_hex_compact(start);
    print("-0x");
    print_hex_compact(end);
    print(" (zone ");
    print_hex_compact(zone);
    print(")\n");
}

memory_region_t *memory_region_find(uintptr_t virt_addr)
{
    for (int i = 0; i < region_count; i++)
    {
        if (virt_addr >= memory_regions[i].start &&
            virt_addr < memory_regions[i].end)
        {
            return &memory_regions[i];
        }
    }
    return NULL;
}

int memory_region_conflicts(uintptr_t start, uintptr_t end)
{
    for (int i = 0; i < region_count; i++)
    {
        uintptr_t reg_start = memory_regions[i].start;
        uintptr_t reg_end = memory_regions[i].end;

        // Verificar solapamiento
        if (!(end <= reg_start || start >= reg_end))
        {
            return 1; // Hay conflicto
        }
    }
    return 0; // No hay conflicto
}

#endif // MEMORY_LAYOUT_IMPLEMENTATION