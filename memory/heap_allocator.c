// memory/memory_interface.c - Implementación de la interfaz común
#include "memo_interface.h"
#include "heap_allocator.h"
#include "physical_allocator.c"
#include <print.h>
#include <panic/panic.h>
#include <string.h>

// Variables globales para estadísticas (definidas en physical_allocator.c)
extern uint32_t free_pages_count;
extern uint32_t total_pages_count;
static heap_block_t *heap_start = NULL; // Puntero al primer bloque del heap
static size_t heap_total_size = 0;

// Prototipos de funciones del physical allocator
extern void physical_allocator_init(void);
extern uintptr_t alloc_physical_page(void);
extern void free_physical_page(uintptr_t phys_addr);

// acá voy a tener mis implementaciones de malloc, reallloc y free.
extern void heap_allocator_init(void);
extern void *kmalloc_impl(size_t size);
extern void kfree_impl(void *ptr);

static int memory_system_initialized = 0; // flag de control para saber si el subsistema de memoria está iniciado.

void memory_init(void)
{
    if (memory_system_initialized)
    {
        LOG_WARN("Memory system already initialized");
        return;
    }

    print_colored("=== INICIALIZANDO SISTEMA DE MEMORIA ===\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);

    // 1. Inicializar el allocator de páginas físicas
    physical_allocator_init();
    LOG_OK("Physical allocator inicializado");

    // 2. Inicializar el heap allocator (kmalloc/kfree)
    heap_allocator_init();
    LOG_OK("Heap allocator inicializado");

    memory_system_initialized = 1;
    print_success("Sistema de memoria completamente inicializado\n");
}

// Agregar esto al heap_allocator.c

void *krealloc(void *ptr, size_t new_size)
{
    // Caso 1: ptr es NULL → actúa como malloc
    if (!ptr)
    {
        return kmalloc_impl(new_size);
    }

    // Caso 2: new_size es 0 → actúa como free
    if (new_size == 0)
    {
        kfree_impl(ptr);
        return NULL;
    }

    // Validar puntero
    if (!is_valid_heap_pointer(ptr))
    {
        LOG_ERR("krealloc: Invalid pointer!");
        return NULL;
    }

    // Obtener bloque actual
    heap_block_t *current_block = (heap_block_t *)((uint8_t *)ptr - sizeof(heap_block_t));

    if (current_block->magic != HEAP_MAGIC)
    {
        LOG_ERR("krealloc: Corrupted block!");
        return NULL;
    }

    size_t old_size = current_block->size;

    // Alinear nuevo tamaño
    new_size = (new_size + 7) & ~7;

    // Caso 3: El nuevo tamaño cabe en el bloque actual
    if (new_size <= old_size)
    {
        // Si es mucho más chico, dividir el bloque
        if (old_size > new_size + sizeof(heap_block_t) + MIN_BLOCK_SIZE)
        {
            split_block(current_block, new_size);

            // Actualizar estadísticas
            size_t freed_bytes = old_size - new_size;
            heap_used_bytes -= freed_bytes;
            heap_free_bytes += freed_bytes;
        }
        return ptr; // Retornar el mismo puntero
    }

    // Caso 4: Intentar expandir el bloque actual en un Realloc()
    // Verificar si el siguiente bloque está libre y es suficiente
    heap_block_t *next_block = current_block->next;
    if (next_block && next_block->is_free)
    {
        size_t combined_size = old_size + sizeof(heap_block_t) + next_block->size;

        if (combined_size >= new_size)
        {
            // Fusionar con el siguiente bloque
            current_block->size += sizeof(heap_block_t) + next_block->size;
            current_block->next = next_block->next;

            if (next_block->next)
            {
                next_block->next->prev = current_block;
            }

            // Actualizar estadísticas
            heap_used_bytes += (current_block->size - old_size);
            heap_free_bytes -= (current_block->size - old_size);

            // Si el bloque combinado es muy grande, dividirlo
            if (current_block->size > new_size + sizeof(heap_block_t) + MIN_BLOCK_SIZE)
            {
                split_block(current_block, new_size);

                size_t excess = current_block->size - new_size;
                heap_used_bytes -= excess;
                heap_free_bytes += excess;
            }

            return ptr; // Retornar el mismo puntero
        }
    }

    // Caso 5: No se puede expandir → allocar nuevo bloque y copiar
    void *new_ptr = kmalloc_impl(new_size);
    if (!new_ptr)
    {
        return NULL; // No hay memoria
    }

    // Copiar datos del bloque viejo al nuevo
    size_t copy_size = (old_size < new_size) ? old_size : new_size;
    memcpy(new_ptr, ptr, copy_size);

    // Liberar bloque viejo
    kfree_impl(ptr);

    return new_ptr;
}

// Función wrapper para compatibilidad esta es la API que expongo al resto del kernel
void *realloc(void *ptr, size_t size)
{
    return krealloc(ptr, size);
}

void *kmalloc(size_t size)
{
    if (!memory_system_initialized)
    {
        memory_init();
    }

    return kmalloc_impl(size);
}

void kfree(void *ptr)
{
    if (!memory_system_initialized)
    {
        LOG_ERR("kfree: Memory system not initialized");
        return;
    }

    kfree_impl(ptr);
}

int map_page(uintptr_t virt_addr, uintptr_t phys_addr, uint32_t flags)
{
    if (!memory_system_initialized)
    {
        LOG_ERR("map_page: Memory system not initialized");
        return -1;
    }

    // Si phys_addr es 0, allocar una página física automáticamente
    if (phys_addr == 0)
    {
        phys_addr = alloc_physical_page();
        if (phys_addr == 0)
        {
            LOG_ERR("map_page: No se pudo allocar página física");
            return -2;
        }
    }

    // Llamar a la función específica de arquitectura
    int result = arch_map_page(virt_addr, phys_addr, flags);

    if (result != 0)
    {
        // Si falló el mapeo y nosotros allocamos la página, liberarla
        if (phys_addr != 0)
        {
            free_physical_page(phys_addr);
        }
        LOG_ERR("map_page: Error en mapeo específico de arquitectura");
        return result;
    }

    return 0;
}

int unmap_page(uintptr_t virt_addr)
{
    if (!memory_system_initialized)
    {
        LOG_ERR("unmap_page: Memory system not initialized");
        return -1;
    }

    // Obtener dirección física antes de unmapear
    uintptr_t phys_addr = arch_virt_to_phys(virt_addr);

    // Unmapear usando función específica de arquitectura
    int result = arch_unmap_page(virt_addr);

    if (result == 0 && phys_addr != 0)
    {
        // Si el unmapeo fue exitoso, liberar la página física
        free_physical_page(phys_addr);
    }

    return result;
}

uintptr_t virt_to_phys(uintptr_t virt_addr)
{
    if (!memory_system_initialized)
    {
        return 0;
    }

    return arch_virt_to_phys(virt_addr);
}

bool is_valid_heap_pointer(void *ptr)
{
    if (!ptr)
        return false;

    heap_block_t *block = (heap_block_t *)((uint8_t *)ptr - sizeof(heap_block_t));

    // Verificamos que el bloque esté dentro del heap
    if ((void *)block < (void *)heap_start)
        return false;

    if (block->magic != HEAP_MAGIC)
        return false;

    return true;
}

void split_block(heap_block_t *block, size_t wanted_size)
{
    if (block->size < wanted_size + sizeof(heap_block_t) + MIN_BLOCK_SIZE)
    {
        // No se puede dividir, demasiado pequeño
        LOG_ERR("Block to small to divide it");
        return;
    }

    heap_block_t *new_block = (heap_block_t *)((uint8_t *)block + sizeof(heap_block_t) + wanted_size);
    new_block->size = block->size - wanted_size - sizeof(heap_block_t);
    new_block->next = block->next;
    new_block->prev = block;
    new_block->is_free = 1;
    new_block->magic = HEAP_MAGIC;

    if (block->next)
    {
        block->next->prev = new_block;
    }

    block->next = new_block;
    block->size = wanted_size;
}

void kfree_impl(void *ptr)
{
    if (!is_valid_heap_pointer(ptr))
    {
        LOG_ERR("invalid memo at this position.");
        return;
    }

    heap_block_t *block = (heap_block_t *)((uint8_t *)ptr - sizeof(heap_block_t));
    block->is_free = 1;

    // Intentar fusionar con el siguiente bloque libre
    if (block->next && block->next->is_free)
    {
        block->size += sizeof(heap_block_t) + block->next->size;
        block->next = block->next->next;
        if (block->next)
        {
            block->next->prev = block;
        }
    }

    // Intentar fusionar con el bloque anterior libre
    if (block->prev && block->prev->is_free)
    {
        block->prev->size += sizeof(heap_block_t) + block->size;
        block->prev->next = block->next;
        if (block->next)
        {
            block->next->prev = block->prev;
        }
    }
}

void *kmalloc_impl(size_t size)
{
    heap_block_t *current = heap_start;

    // Alinear tamaño a múltiplo de 8 para hacerlo mas eficiente y pq las arquitecturas me lo exigen si quiero evitar aligment faults.
    size = (size + 7) & ~7;

    while (current)
    {
        if (current->is_free && current->size >= size)
        {
            split_block(current, size);
            current->is_free = 0;
            return (void *)((uint8_t *)current + sizeof(heap_block_t));
        }
        current = current->next;
    }

    return NULL;
}

void debug_memory_state(void)
{
    print_colored("=== ESTADO COMPLETO DEL SISTEMA DE MEMORIA ===\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);

    // Estadísticas generales
    print("Total pages: ");
    print_hex_compact(total_pages_count);
    print("\n");

    print("Free pages: ");
    print_hex_compact(free_pages_count);
    print("\n");

    print("Used pages: ");
    print_hex_compact(total_pages_count - free_pages_count);
    print("\n");

    if (total_pages_count > 0)
    {
        uint32_t usage_percent = ((total_pages_count - free_pages_count) * 100) / total_pages_count;
        print("Memory usage: ");
        print_hex_compact(usage_percent);
        print("%\n");
    }

    print("Memory system initialized: ");
    print(memory_system_initialized ? "YES" : "NO");
    print("\n\n");

    // Llamar funciones de debugging específicas
    extern void debug_physical_allocator(void);
    extern void debug_heap_allocator(void);

    debug_physical_allocator();
    debug_heap_allocator();
}

// Funciones de conveniencia para compatibilidad
void *malloc(size_t size)
{
    return kmalloc(size);
}

void free(void *ptr)
{
    kfree(ptr);
}