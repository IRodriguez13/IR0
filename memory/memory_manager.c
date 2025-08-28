// ===============================================================================
// IR0 KERNEL - MODULAR MEMORY MANAGEMENT SYSTEM IMPLEMENTATION
// ===============================================================================

#include <memory_manager.h>
#include <bump_allocator.h>
#include <ir0/print.h>
#include <ir0/panic/panic.h>
#include <string.h>

// ===============================================================================
// GLOBAL VARIABLES
// ===============================================================================

memory_manager_t *g_memory_manager = NULL;

// ===============================================================================
// BUMP ALLOCATOR WRAPPER
// ===============================================================================

// Wrapper para el bump allocator existente
static void *bump_allocator_alloc(memory_allocator_t *allocator, size_t size)
{
    (void)allocator; // No usar allocator en esta implementación básica
    return kmalloc(size);
}

static void bump_allocator_free(memory_allocator_t *allocator, void *ptr)
{
    (void)allocator; // No usar allocator en esta implementación básica
    kfree(ptr);
}

static void *bump_allocator_realloc(memory_allocator_t *allocator, void *ptr, size_t new_size)
{
    (void)allocator; // No usar allocator en esta implementación básica
    return krealloc(ptr, new_size);
}

static size_t bump_allocator_get_size(memory_allocator_t *allocator, void *ptr)
{
    (void)allocator;
    (void)ptr;
    // Por ahora retornamos 0, se puede implementar después
    return 0;
}

static bool bump_allocator_is_valid(memory_allocator_t *allocator, void *ptr)
{
    (void)allocator;
    (void)ptr;
    // Por ahora siempre retornamos true, se puede implementar validación después
    return true;
}

static void bump_allocator_defragment(memory_allocator_t *allocator)
{
    (void)allocator;
    // Por ahora no hacemos nada, se puede implementar después
}

// ===============================================================================
// UTILITY FUNCTIONS
// ===============================================================================

size_t memory_align_size(size_t size, size_t alignment)
{
    return (size + alignment - 1) & ~(alignment - 1);
}

uintptr_t memory_align_addr(uintptr_t addr, size_t alignment)
{
    return (addr + alignment - 1) & ~(alignment - 1);
}

void memory_zero(void *ptr, size_t size)
{
    memset(ptr, 0, size);
}

void memory_copy(void *dest, const void *src, size_t size)
{
    memcpy(dest, src, size);
}

void memory_set(void *ptr, int value, size_t size)
{
    memset(ptr, value, size);
}

// ===============================================================================
// CORE MEMORY MANAGER FUNCTIONS
// ===============================================================================

int memory_manager_init(void)
{
    print("Inicializando Memory Manager...\n");

    // Inicializar el bump allocator existente
    heap_init();

    // Crear el gestor de memoria global
    g_memory_manager = kmalloc(sizeof(memory_manager_t));
    if (!g_memory_manager)
    {
        panic("No se pudo asignar memoria para el Memory Manager\n");
        return -1;
    }

    // Inicializar estructura
    memory_zero(g_memory_manager, sizeof(memory_manager_t));

    // Configurar zonas de memoria básicas
    g_memory_manager->zones[MEMORY_ZONE_DMA].type = MEMORY_ZONE_DMA;
    g_memory_manager->zones[MEMORY_ZONE_DMA].start_addr = 0x00000000;
    g_memory_manager->zones[MEMORY_ZONE_DMA].end_addr = 0x01000000; // 16MB
    g_memory_manager->zones[MEMORY_ZONE_DMA].total_size = 0x01000000;
    g_memory_manager->zones[MEMORY_ZONE_DMA].free_size = 0x01000000;

    g_memory_manager->zones[MEMORY_ZONE_NORMAL].type = MEMORY_ZONE_NORMAL;
    g_memory_manager->zones[MEMORY_ZONE_NORMAL].start_addr = 0x01000000;
    g_memory_manager->zones[MEMORY_ZONE_NORMAL].end_addr = 0x38000000; // 896MB
    g_memory_manager->zones[MEMORY_ZONE_NORMAL].total_size = 0x37000000;
    g_memory_manager->zones[MEMORY_ZONE_NORMAL].free_size = 0x37000000;

    g_memory_manager->zones[MEMORY_ZONE_HIGHMEM].type = MEMORY_ZONE_HIGHMEM;
    g_memory_manager->zones[MEMORY_ZONE_HIGHMEM].start_addr = 0x38000000;
    g_memory_manager->zones[MEMORY_ZONE_HIGHMEM].end_addr = 0xFFFFFFFF; // 4GB
    g_memory_manager->zones[MEMORY_ZONE_HIGHMEM].total_size = 0xC8000000;
    g_memory_manager->zones[MEMORY_ZONE_HIGHMEM].free_size = 0xC8000000;

    // Configurar allocator por defecto (bump allocator)
    g_memory_manager->default_allocator = kmalloc(sizeof(memory_allocator_t));
    if (!g_memory_manager->default_allocator)
    {
        panic("No se pudo asignar memoria para el allocator por defecto\n");
        return -1;
    }

    g_memory_manager->default_allocator->name = "Bump Allocator";
    g_memory_manager->default_allocator->type = ALLOCATOR_BUMP;
    g_memory_manager->default_allocator->alloc = bump_allocator_alloc;
    g_memory_manager->default_allocator->free = bump_allocator_free;
    g_memory_manager->default_allocator->realloc = bump_allocator_realloc;
    g_memory_manager->default_allocator->get_allocated_size = bump_allocator_get_size;
    g_memory_manager->default_allocator->is_valid_ptr = bump_allocator_is_valid;
    g_memory_manager->default_allocator->defragment = bump_allocator_defragment;

    // Configurar estadísticas iniciales
    g_memory_manager->total_memory = get_heap_total();
    g_memory_manager->free_memory = get_heap_free();
    g_memory_manager->used_memory = get_heap_used();

    // Configurar zonas para usar el allocator por defecto
    for (int i = 0; i < MEMORY_ZONE_COUNT; i++)
    {
        g_memory_manager->zones[i].primary_allocator = g_memory_manager->default_allocator;
    }

    print("Memory Manager inicializado correctamente\n");
    print("Total memory: ");
    print_uint64(g_memory_manager->total_memory);
    print(" bytes\n");
    print("Free memory: ");
    print_uint64(g_memory_manager->free_memory);
    print(" bytes\n");
    print("Used memory: ");
    print_uint64(g_memory_manager->used_memory);
    print(" bytes\n");

    return 0;
}

void memory_manager_shutdown(void)
{
    if (g_memory_manager)
    {
        if (g_memory_manager->default_allocator)
        {
            kfree(g_memory_manager->default_allocator);
        }

        kfree(g_memory_manager);
        g_memory_manager = NULL;
    }
}

// ===============================================================================
// ZONE MANAGEMENT
// ===============================================================================

memory_zone_t *memory_get_zone(memory_zone_type_t type)
{
    if (!g_memory_manager || type >= MEMORY_ZONE_COUNT)
    {
        return NULL;
    }
    return &g_memory_manager->zones[type];
}

memory_zone_t *memory_get_zone_for_addr(uintptr_t addr)
{
    if (!g_memory_manager)
    {
        return NULL;
    }

    for (int i = 0; i < MEMORY_ZONE_COUNT; i++)
    {
        if (addr >= g_memory_manager->zones[i].start_addr &&
            addr < g_memory_manager->zones[i].end_addr)
        {
            return &g_memory_manager->zones[i];
        }
    }

    return NULL;
}

// ===============================================================================
// ALLOCATION FUNCTIONS
// ===============================================================================

void *memory_alloc(size_t size)
{
    if (!g_memory_manager || !g_memory_manager->default_allocator)
    {
        return NULL;
    }

    void *ptr = g_memory_manager->default_allocator->alloc(g_memory_manager->default_allocator, size);

    if (ptr)
    {
        g_memory_manager->default_allocator->total_allocated += size;
        g_memory_manager->default_allocator->current_usage += size;
        if (g_memory_manager->default_allocator->current_usage > g_memory_manager->default_allocator->peak_usage)
        {
            g_memory_manager->default_allocator->peak_usage = g_memory_manager->default_allocator->current_usage;
        }

        // Actualizar estadísticas globales
        g_memory_manager->used_memory = get_heap_used();
        g_memory_manager->free_memory = get_heap_free();
    }

    return ptr;
}

void *memory_alloc_aligned(size_t size, size_t alignment)
{
    // Por ahora usamos la función básica, se puede optimizar después
    size_t aligned_size = memory_align_size(size, alignment);
    return memory_alloc(aligned_size);
}

void memory_free(void *ptr)
{
    if (!g_memory_manager || !g_memory_manager->default_allocator || !ptr)
    {
        return;
    }

    // Por ahora no podemos obtener el tamaño exacto, así que estimamos
    // Esto se puede mejorar cuando implementemos get_allocated_size
    size_t estimated_size = 16; // Estimación básica

    g_memory_manager->default_allocator->free(g_memory_manager->default_allocator, ptr);

    g_memory_manager->default_allocator->total_freed += estimated_size;
    if (g_memory_manager->default_allocator->current_usage >= estimated_size)
    {
        g_memory_manager->default_allocator->current_usage -= estimated_size;
    }
    else
    {
        g_memory_manager->default_allocator->current_usage = 0;
    }

    // Actualizar estadísticas globales
    g_memory_manager->used_memory = get_heap_used();
    g_memory_manager->free_memory = get_heap_free();
}

void *memory_realloc(void *ptr, size_t new_size)
{
    if (!g_memory_manager || !g_memory_manager->default_allocator)
    {
        return NULL;
    }

    if (!ptr)
    {
        return memory_alloc(new_size);
    }

    if (new_size == 0)
    {
        memory_free(ptr);
        return NULL;
    }

    void *new_ptr = g_memory_manager->default_allocator->realloc(g_memory_manager->default_allocator, ptr, new_size);

    if (new_ptr && new_ptr != ptr)
    {
        // Actualizar estadísticas si el puntero cambió
        size_t estimated_old_size = 16; // Estimación básica
        g_memory_manager->default_allocator->total_freed += estimated_old_size;
        g_memory_manager->default_allocator->total_allocated += new_size;

        if (g_memory_manager->default_allocator->current_usage >= estimated_old_size)
        {
            g_memory_manager->default_allocator->current_usage -= estimated_old_size;
        }
        g_memory_manager->default_allocator->current_usage += new_size;

        if (g_memory_manager->default_allocator->current_usage > g_memory_manager->default_allocator->peak_usage)
        {
            g_memory_manager->default_allocator->peak_usage = g_memory_manager->default_allocator->current_usage;
        }
    }

    return new_ptr;
}

void *memory_calloc(size_t nmemb, size_t size)
{
    size_t total_size = nmemb * size;
    void *ptr = memory_alloc(total_size);

    if (ptr)
    {
        memory_zero(ptr, total_size);
    }

    return ptr;
}

// ===============================================================================
// ZONE-SPECIFIC ALLOCATION
// ===============================================================================

void *memory_alloc_in_zone(memory_zone_t *zone, size_t size)
{
    // Por ahora todas las zonas usan el mismo allocator
    (void)zone; // No usar zone en esta implementación básica
    return memory_alloc(size);
}

void memory_free_in_zone(memory_zone_t *zone, void *ptr)
{
    (void)zone; // No usar zone en esta implementación básica
    memory_free(ptr);
}

// ===============================================================================
// DEBUG AND MONITORING FUNCTIONS
// ===============================================================================

void memory_print_stats(void)
{
    if (!g_memory_manager)
    {
        print("Memory Manager no inicializado\n");
        return;
    }

    print("=== MEMORY MANAGER STATISTICS ===\n");
    print("Total Memory: ");
    print_uint64(g_memory_manager->total_memory);
    print(" bytes\n");
    print("Used Memory: ");
    print_uint64(g_memory_manager->used_memory);
    print(" bytes\n");
    print("Free Memory: ");
    print_uint64(g_memory_manager->free_memory);
    print(" bytes\n");

    if (g_memory_manager->default_allocator)
    {
        print("\n=== DEFAULT ALLOCATOR STATISTICS ===\n");
        print("Name: ");
        print(g_memory_manager->default_allocator->name);
        print("\n");
        print("Type: ");
        print_uint64(g_memory_manager->default_allocator->type);
        print("\n");
        print("Total Allocated: ");
        print_uint64(g_memory_manager->default_allocator->total_allocated);
        print(" bytes\n");
        print("Total Freed: ");
        print_uint64(g_memory_manager->default_allocator->total_freed);
        print(" bytes\n");
        print("Current Usage: ");
        print_uint64(g_memory_manager->default_allocator->current_usage);
        print(" bytes\n");
        print("Peak Usage: ");
        print_uint64(g_memory_manager->default_allocator->peak_usage);
        print(" bytes\n");
    }

    print("\n=== ZONE STATISTICS ===\n");
    for (int i = 0; i < MEMORY_ZONE_COUNT; i++)
    {
        memory_zone_t *zone = &g_memory_manager->zones[i];
        print("Zone ");
        print_uint64(i);
        print(": ");
        print_uint64(zone->total_size);
        print(" bytes\n");
    }
}

void memory_print_zone_stats(memory_zone_t *zone)
{
    if (!zone)
    {
        print("Zone is NULL\n");
        return;
    }
    print("Zone ");
    print_uint64(zone->type);
    print(": ");
    print_uint64(zone->total_size);
    print(" bytes\n");
}

void memory_print_allocator_stats(memory_allocator_t *allocator)
{
    if (!allocator)
    {
        print("Allocator is NULL\n");
        return;
    }

    print("Allocator: ");
    print(allocator->name);
    print("\n");
    print("Type: ");
    print_uint64(allocator->type);
    print("\n");
    print("Total Allocated: ");
    print_uint64(allocator->total_allocated);
    print(" bytes\n");
    print("Total Freed: ");
    print_uint64(allocator->total_freed);
    print(" bytes\n");
    print("Current Usage: ");
    print_uint64(allocator->current_usage);
    print(" bytes\n");
    print("Peak Usage: ");
    print_uint64(allocator->peak_usage);
    print(" bytes\n");
}

// ===============================================================================
// VALIDATION FUNCTIONS
// ===============================================================================

bool memory_validate_ptr(void *ptr)
{
    if (!g_memory_manager || !g_memory_manager->default_allocator)
    {
        return false;
    }

    return g_memory_manager->default_allocator->is_valid_ptr(g_memory_manager->default_allocator, ptr);
}

void memory_validate_heap(void)
{
    if (!g_memory_manager)
    {
        print("Memory Manager no inicializado\n");
        return;
    }

    heap_validate_integrity();
}

void memory_dump_heap(void)
{
    if (!g_memory_manager)
    {
        print("Memory Manager no inicializado\n");
        return;
    }

    heap_dump_info();
}

// ===============================================================================
// CONFIGURATION FUNCTIONS
// ===============================================================================

void memory_set_default_allocator(allocator_type_t type)
{
    if (!g_memory_manager)
    {
        return;
    }

    // Por ahora solo soportamos BUMP allocator
    if (type != ALLOCATOR_BUMP)
    {
        print("Solo se soporta ALLOCATOR_BUMP por ahora\n");
        return;
    }

    g_memory_manager->default_allocator->type = type;
}

void memory_set_zone_allocator(memory_zone_type_t zone, allocator_type_t type)
{
    if (!g_memory_manager || zone >= MEMORY_ZONE_COUNT)
    {
        return;
    }

    // Por ahora todas las zonas usan el mismo allocator
    memory_set_default_allocator(type);
}

void memory_enable_slabs(bool enable)
{
    if (!g_memory_manager)
    {
        return;
    }

    g_memory_manager->enable_slabs = enable;
    print("Slab allocator ");
    print(enable ? "habilitado" : "deshabilitado");
    print("\n");
}

void memory_enable_buddy(bool enable)
{
    if (!g_memory_manager)
    {
        return;
    }

    g_memory_manager->enable_buddy = enable;
    print("Buddy allocator ");
    print(enable ? "habilitado" : "deshabilitado");
    print("\n");
}

void memory_enable_debug(bool enable)
{
    if (!g_memory_manager)
    {
        return;
    }

    g_memory_manager->enable_debug = enable;
    print("Debug mode ");
    print(enable ? "habilitado" : "deshabilitado");
    print("\n");
}

void memory_set_debug_callback(void (*callback)(const char *msg))
{
    if (!g_memory_manager)
    {
        return;
    }

    g_memory_manager->debug_callback = callback;
}

void memory_set_error_callback(void (*callback)(const char *msg))
{
    if (!g_memory_manager)
    {
        return;
    }

    g_memory_manager->error_callback = callback;
}
