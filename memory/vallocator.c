// memory/virtual_allocator.c - Virtual malloc mejorado y thread-safe

#include "memo_interface.h"
#include "ondemand-paging.h"
#include <print.h>
#include <panic/panic.h>
#include <string.h>
#define MEMORY_LAYOUT_IMPLEMENTATION
#include "krnl_memo_layout.h"

// ===============================================================================
// VIRTUAL MEMORY ALLOCATOR - Reemplazo completo de vmalloc básico
// ===============================================================================

#define MAX_VMALLOC_REGIONS 256
#define VMALLOC_ALIGNMENT 4096 // Alinear a páginas

typedef struct vmalloc_region
{
    uintptr_t start;
    size_t size;
    uint32_t flags;
    int in_use;
    struct vmalloc_region *next;
} vmalloc_region_t;

static vmalloc_region_t vmalloc_regions[MAX_VMALLOC_REGIONS];
static vmalloc_region_t *free_regions = NULL;
static vmalloc_region_t *used_regions = NULL;
static int vmalloc_initialized = 0;
static uintptr_t vmalloc_next_addr = VMALLOC_BASE;

// Estadísticas
static size_t total_vmalloc_bytes = 0;
static size_t used_vmalloc_bytes = 0;
static int vmalloc_allocations = 0;

// ===============================================================================
// INICIALIZACIÓN
// ===============================================================================

void virtual_allocator_init(void)
{
    if (vmalloc_initialized)
    {
        return;
    }

    LOG_OK("Inicializando virtual allocator");

    // Inicializar pool de regiones
    for (int i = 0; i < MAX_VMALLOC_REGIONS - 1; i++)
    {
        vmalloc_regions[i].next = &vmalloc_regions[i + 1];
        vmalloc_regions[i].in_use = 0;
    }
    vmalloc_regions[MAX_VMALLOC_REGIONS - 1].next = NULL;
    vmalloc_regions[MAX_VMALLOC_REGIONS - 1].in_use = 0;

    free_regions = &vmalloc_regions[0];
    used_regions = NULL;

    // Registrar la zona vmalloc en el sistema unificado
    memory_region_register(VMALLOC_BASE, VMALLOC_END, ZONE_VMALLOC,
                           PAGE_FLAG_PRESENT | PAGE_FLAG_WRITABLE, 0, 1);

    // Registrar en el sistema on-demand
    vm_area_register(VMALLOC_BASE, VMALLOC_END,
                     PAGE_FLAG_PRESENT | PAGE_FLAG_WRITABLE);

    vmalloc_initialized = 1;
    LOG_OK("Virtual allocator inicializado (zona: 0x%08X - 0x%08X)");
    print_hex_compact(VMALLOC_BASE);
    print_hex_compact(VMALLOC_END);
}

// ===============================================================================
// GESTIÓN DE REGIONES
// ===============================================================================

vmalloc_region_t *allocate_region_descriptor(void)
{
    if (!free_regions)
    {
        LOG_ERR("vmalloc: Sin descriptores de región disponibles");
        return NULL;
    }

    vmalloc_region_t *region = free_regions;
    free_regions = region->next;

    // Agregar a lista de usados
    region->next = used_regions;
    used_regions = region;
    region->in_use = 1;

    return region;
}

void free_region_descriptor(vmalloc_region_t *region)
{
    if (!region || !region->in_use)
    {
        return;
    }

    // Remover de lista de usados
    if (used_regions == region)
    {
        used_regions = region->next;
    }
    else
    {
        vmalloc_region_t *current = used_regions;
        while (current && current->next != region)
        {
            current = current->next;
        }
        if (current)
        {
            current->next = region->next;
        }
    }

    // Agregar a lista de libres
    region->next = free_regions;
    free_regions = region;
    region->in_use = 0;
}

uintptr_t find_free_virtual_space(size_t size)
{
    size = PAGE_ALIGN(size); // Alinear a páginas

    // Buscar espacio libre en las regiones usadas
    vmalloc_region_t *current = used_regions;
    uintptr_t candidate = VMALLOC_BASE;

    while (current)
    {
        if (candidate + size <= current->start)
        {
            // Encontramos hueco antes de esta región
            return candidate;
        }

        // Mover después de esta región
        candidate = current->start + PAGE_ALIGN(current->size);
        current = current->next;
    }

    // Verificar si cabe al final
    if (candidate + size <= VMALLOC_END)
    {
        return candidate;
    }

    LOG_ERR("vmalloc: Sin espacio virtual disponible");
    return 0;
}

// ===============================================================================
// API PÚBLICA MEJORADA
// ===============================================================================

void *vmalloc(size_t size)
{
    if (!vmalloc_initialized)
    {
        virtual_allocator_init();
    }

    if (size == 0)
    {
        return NULL;
    }

    size = PAGE_ALIGN(size);

    // Encontrar espacio virtual libre
    uintptr_t virt_addr = find_free_virtual_space(size);
    if (virt_addr == 0)
    {
        LOG_ERR("vmalloc: No se puede encontrar espacio virtual");
        return NULL;
    }

    // Allocar descriptor de región
    vmalloc_region_t *region = allocate_region_descriptor();
    if (!region)
    {
        LOG_ERR("vmalloc: No se puede allocar descriptor");
        return NULL;
    }

    // Configurar región
    region->start = virt_addr;
    region->size = size;
    region->flags = PAGE_FLAG_PRESENT | PAGE_FLAG_WRITABLE;

    // CRÍTICO: NO allocar páginas físicas aquí
    // El sistema on-demand las allocará cuando se acceda

    // Actualizar estadísticas
    total_vmalloc_bytes += size;
    used_vmalloc_bytes += size; // Se cuenta como usado aunque no tenga páginas físicas
    vmalloc_allocations++;

    return (void *)virt_addr;
}

void vfree(void *ptr)
{
    if (!ptr || !vmalloc_initialized)
    {
        return;
    }

    uintptr_t virt_addr = (uintptr_t)ptr;

    // Verificar que esté en zona vmalloc
    if (get_memory_zone(virt_addr) != ZONE_VMALLOC)
    {
        LOG_ERR("vfree: Dirección fuera de zona vmalloc");
        return;
    }

    // Encontrar región correspondiente
    vmalloc_region_t *region = used_regions;
    while (region)
    {
        if (region->start == virt_addr)
        {
            break;
        }
        region = region->next;
    }

    if (!region)
    {
        LOG_ERR("vfree: Región no encontrada para dirección 0x%08X");
        print_hex_compact(virt_addr);
        return;
    }

    // Unmapear todas las páginas de la región
    for (uintptr_t addr = region->start;
         addr < region->start + region->size;
         addr += PAGE_SIZE)
    {

        // Solo unmapear si está mapeada (puede estar sin mapear por lazy allocation)
        uintptr_t phys = arch_virt_to_phys(addr);
        if (phys != 0)
        {
            unmap_page(addr);
        }
    }

    // Actualizar estadísticas
    used_vmalloc_bytes -= region->size;
    vmalloc_allocations--;

    // Liberar descriptor
    free_region_descriptor(region);

    LOG_OK("vfree: Liberada región en 0x%08X (%u bytes)");
    print_hex_compact(virt_addr);
    print_hex_compact(region->size);
}

// ===============================================================================
// FUNCIONES ADICIONALES
// ===============================================================================

void *vzalloc(size_t size)
{
    void *ptr = vmalloc(size);
    if (ptr)
    {
        // NOTA: memset aquí forzará page faults y allocation física
        memset(ptr, 0, size);
    }
    return ptr;
}

void *vmalloc_user(size_t size)
{
    // Para futuro: vmalloc que puede ser accedido por user space
    void *ptr = vmalloc(size);
    if (ptr)
    {
        // TODO: Marcar páginas como USER accessible cuando se mapeen
    }
    return ptr;
}

// ===============================================================================
// DEBUG Y ESTADÍSTICAS
// ===============================================================================

void debug_virtual_allocator(void)
{
    print_colored("=== VIRTUAL ALLOCATOR STATE ===\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);

    print("Zone: 0x");
    print_hex_compact(VMALLOC_BASE);
    print(" - 0x");
    print_hex_compact(VMALLOC_END);
    print("\n");

    print("Total allocations: ");
    print_hex_compact(vmalloc_allocations);
    print("\n");

    print("Total virtual bytes: ");
    print_hex_compact(total_vmalloc_bytes);
    print("\n");

    print("Used virtual bytes: ");
    print_hex_compact(used_vmalloc_bytes);
    print("\n");

    // Mostrar regiones activas
    print("Active regions:\n");
    vmalloc_region_t *current = used_regions;
    int count = 0;

    while (current && count < 10)
    {
        print("  Region ");
        print_hex_compact(count);
        print(": 0x");
        print_hex_compact(current->start);
        print(" - 0x");
        print_hex_compact(current->start + current->size);
        print(" (");
        print_hex_compact(current->size);
        print(" bytes)\n");

        current = current->next;
        count++;
    }

    if (current)
    {
        print("  ... (more regions)\n");
    }

    print("\n");
}