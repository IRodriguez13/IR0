#include <bump_allocator.h>
#include <ir0/panic/panic.h>
#include <ir0/print.h>
#include <string.h>
#include <stdarg.h>

extern char _end; // definido en el linker

// === CONFIGURACIÓN DEL HEAP ===
#define HEAP_SIZE 0x1E00000                          // 30MB - usando el rango libre mapeado expandido
#define HEAP_START 0x200000                          // 2MB desde el inicio (después del kernel)
#define MIN_BLOCK_SIZE (sizeof(block_header_t) + 16) // Tamaño mínimo útil
#define ALIGNMENT 16                                 // Alineación por defecto

// === VARIABLES GLOBALES ===
uint8_t *heap_base = (uint8_t *)HEAP_START;
uint8_t *heap_end = (uint8_t *)HEAP_START + HEAP_SIZE;
uint8_t *heap_ptr = (uint8_t *)HEAP_START;
block_header_t *free_list_head = NULL;
allocation_strategy_t current_strategy = FIRST_FIT;

// === FUNCIONES AUXILIARES ===

// Alinear dirección a múltiplo de alignment
static inline uintptr_t align_up(uintptr_t addr, size_t alignment)
{
    return (addr + alignment - 1) & ~(alignment - 1);
}

// Obtener header de un bloque desde su puntero de datos
static inline block_header_t *get_block_header(void *ptr)
{
    return (block_header_t *)((uint8_t *)ptr - sizeof(block_header_t));
}

// Obtener puntero de datos desde header
static inline void *get_block_data(block_header_t *header)
{
    return (void *)((uint8_t *)header + sizeof(block_header_t));
}

// Verificar si un bloque puede ser dividido
static inline int can_split_block(block_header_t *block, size_t requested_size)
{
    return (block->size >= requested_size + MIN_BLOCK_SIZE);
}

// Dividir un bloque en dos
static void split_block(block_header_t *block, size_t requested_size)
{
    size_t remaining_size = block->size - requested_size - sizeof(block_header_t);

    // Crear nuevo bloque con el espacio restante
    block_header_t *new_block = (block_header_t *)((uint8_t *)get_block_data(block) + requested_size);

    // INICIALIZAR COMPLETAMENTE EL NUEVO BLOQUE
    memset(new_block, 0, sizeof(block_header_t)); // Limpiar toda la estructura
    new_block->size = remaining_size;
    new_block->is_free = 1;
    new_block->next = block->next;

    // Actualizar bloque original
    block->size = requested_size;
    block->next = new_block;
}

// Fusionar bloques libres adyacentes
static void merge_adjacent_blocks(void)
{
    if (!free_list_head)
        return;

    block_header_t *current = free_list_head;

    while (current && current->next)
    {
        block_header_t *next = current->next;

        // Verificar que ambos bloques estén libres
        if (!current->is_free || !next->is_free)
        {
            current = current->next;
            continue;
        }

        // Verificar si son contiguos en memoria
        uint8_t *current_end = (uint8_t *)get_block_data(current) + current->size;
        uint8_t *next_start = (uint8_t *)next;

        if (current_end == next_start)
        {
            // Fusionar bloques
            current->size += sizeof(block_header_t) + next->size;
            current->next = next->next;

            // No limpiar el header de next para evitar romper punteros
            // Opcional: limpiar solo la memoria de datos si querés debug
            // memset(get_block_data(next), 0, next->size);

            // No avanzar current, porque podría haber más bloques contiguos
        }
        else
        {
            current = current->next;
        }
    }
}

// Buscar bloque usando estrategia configurada
static block_header_t *find_free_block(size_t size)
{
    block_header_t *best_block = NULL;
    block_header_t *current = free_list_head;

    switch (current_strategy)
    {
    case FIRST_FIT:
        while (current)
        {
            if (current->is_free && current->size >= size)
            {
                return current;
            }
            current = current->next;
        }
        break;

    case BEST_FIT:
        while (current)
        {
            if (current->is_free && current->size >= size)
            {
                if (!best_block || current->size < best_block->size)
                {
                    best_block = current;
                }
            }
            current = current->next;
        }
        break;

    case WORST_FIT:
        while (current)
        {
            if (current->is_free && current->size >= size)
            {
                if (!best_block || current->size > best_block->size)
                {
                    best_block = current;
                }
            }
            current = current->next;
        }
        break;
    }

    return best_block;
}

// === FUNCIONES PRINCIPALES ===

void heap_init(void)
{
    // Limpiar memoria PRIMERO
    memset((void *)HEAP_START, 0, HEAP_SIZE);

    // DESPUÉS inicializar el primer bloque libre (todo el heap)
    block_header_t *first_block = (block_header_t *)HEAP_START;
    first_block->size = HEAP_SIZE - sizeof(block_header_t);
    first_block->is_free = 1;
    first_block->next = NULL;

    free_list_head = first_block;
    heap_ptr = (uint8_t *)HEAP_START;
}

void *kmalloc(size_t size)
{
    if (size == 0)
        return NULL;

    // Alinear tamaño
    size = align_up(size, ALIGNMENT);

    // Buscar bloque libre
    block_header_t *block = find_free_block(size);
    if (!block)
        return NULL;

    // Marcar como ocupado
    block->is_free = 0;

    // PINTAR PATRÓN DE BLOQUE OCUPADO
    void *data_ptr = get_block_data(block);
    memset(data_ptr, 0xAA, block->size);

    // DIVIDIR SI ES NECESARIO - PASO 3
    if (can_split_block(block, size))
    {
        split_block(block, size);
    }

    return data_ptr;
}

void kfree(void *ptr)
{
    if (!ptr)
        return;

    block_header_t *block = get_block_header(ptr);

    // Verificar que el bloque esté ocupado
    if (block->is_free)
        return; // Doble free

    // PINTAR PATRÓN DE BLOQUE LIBRE
    memset(ptr, 0xBB, block->size);

    // Marcar como libre
    block->is_free = 1;

    // FUSIONAR BLOQUES ADYACENTES - PASO 4 (VERSIÓN CORREGIDA)
    merge_adjacent_blocks();
}

void *krealloc(void *ptr, size_t new_size)
{
    if (!ptr)
        return kmalloc(new_size);
    if (new_size == 0)
    {
        kfree(ptr);
        return NULL;
    }

    block_header_t *block = get_block_header(ptr);
    new_size = align_up(new_size, ALIGNMENT);

    // ===== Caso 1: Reducción =====
    if (block->size >= new_size)
    {
        size_t excess_size = block->size - new_size;

        if (excess_size >= MIN_BLOCK_SIZE)
        {
            // Crear bloque libre con el exceso
            block_header_t *new_block = (block_header_t *)((uint8_t *)get_block_data(block) + new_size);
            new_block->size = excess_size - sizeof(block_header_t);
            new_block->is_free = 1;
            new_block->next = block->next;

            block->size = new_size;
            block->next = new_block;

            // Fusionar con bloques adyacentes si es posible
            merge_adjacent_blocks();
        }

        return ptr; // Datos existentes permanecen intactos
    }

    // ===== Caso 2: Expansión in-place =====
    block_header_t *next = block->next;
    if (next && next->is_free)
    {
        size_t total_size = block->size + sizeof(block_header_t) + next->size;
        if (total_size >= new_size)
        {
            // Ajustar bloque actual
            block->size = total_size;
            block->next = next->next;

            // Fusionar con bloques adyacentes restantes
            merge_adjacent_blocks();

            return ptr;
        }
    }

    // ===== Caso 3: Nueva asignación =====
    void *new_ptr = kmalloc(new_size);
    if (new_ptr)
    {
        // Copiar solo el tamaño original del bloque
        memcpy(new_ptr, ptr, block->size);
        kfree(ptr);
    }
    return new_ptr;
}

void *kcalloc(size_t nmemb, size_t size)
{
    // Prevenir overflow
    if (size != 0 && nmemb > SIZE_MAX / size)
        return NULL;

    size_t total_size = nmemb * size;
    void *ptr = kmalloc(total_size);
    if (ptr)
    {
        memset(ptr, 0, total_size);
    }
    return ptr;
}

void *kmalloc_aligned(size_t size, size_t alignment)
{
    // Alineación debe ser potencia de 2
    if (alignment == 0 || (alignment & (alignment - 1)) != 0)
    {
        return NULL;
    }

    // Calcular el tamaño total necesario
    size_t total_size = size + alignment - 1 + sizeof(block_header_t);
    void *ptr = kmalloc(total_size);

    if (!ptr)
        return NULL;

    // Calcular la dirección alineada
    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t aligned_addr = (addr + alignment - 1) & ~(alignment - 1);

    // Si la dirección alineada es diferente, necesitamos crear un nuevo bloque
    if (aligned_addr != addr)
    {
        // Crear un nuevo bloque alineado
        block_header_t *aligned_block = (block_header_t *)(aligned_addr - sizeof(block_header_t));

        // Inicializar el nuevo bloque
        memset(aligned_block, 0, sizeof(block_header_t));
        aligned_block->size = size;
        aligned_block->is_free = 0;

        // Conectar al free list
        aligned_block->next = free_list_head;
        free_list_head = aligned_block;

        return (void *)aligned_addr;
    }

    return ptr;
}

void kfree_aligned(void *ptr)
{
    (void)ptr; // Suppress unused parameter warning
    // NO IMPLEMENTADO - SOLO MÍNIMO
}

// === FUNCIONES DE GESTIÓN ===

void heap_set_strategy(allocation_strategy_t strategy)
{
    current_strategy = strategy;
}

size_t get_heap_used(void)
{
    size_t used = 0;
    block_header_t *current = free_list_head;

    while (current)
    {
        if (!current->is_free)
        {
            used += current->size + sizeof(block_header_t);
        }
        current = current->next;
    }
    return used;
}

size_t get_heap_free(void)
{
    size_t free = 0;
    block_header_t *current = free_list_head;

    while (current)
    {
        if (current->is_free)
        {
            free += current->size + sizeof(block_header_t);
        }
        current = current->next;
    }
    return free;
}

size_t get_heap_total(void)
{
    return HEAP_SIZE;
}

size_t get_heap_fragments(void)
{
    size_t fragments = 0;
    block_header_t *current = free_list_head;

    while (current)
    {
        if (current->is_free)
        {
            fragments++;
        }
        current = current->next;
    }
    return fragments;
}

size_t get_heap_largest_free_block(void)
{
    size_t largest = 0;
    block_header_t *current = free_list_head;

    while (current)
    {
        if (current->is_free && current->size > largest)
        {
            largest = current->size;
        }
        current = current->next;
    }
    return largest;
}

// === FUNCIONES DE DEBUGGING ===

void heap_dump_info(void)
{
    char buffer[256];

    print_success("=== HEAP INFO ===\n");

    sprintf(buffer, "Total: %zu bytes\n", get_heap_total());
    print_success(buffer);

    sprintf(buffer, "Used: %zu bytes\n", get_heap_used());
    print_success(buffer);

    sprintf(buffer, "Free: %zu bytes\n", get_heap_free());
    print_success(buffer);

    sprintf(buffer, "Fragments: %zu\n", get_heap_fragments());
    print_success(buffer);

    sprintf(buffer, "Largest free block: %zu bytes\n", get_heap_largest_free_block());
    print_success(buffer);

    sprintf(buffer, "Strategy: %d\n", current_strategy);
    print_success(buffer);
}

void heap_validate_integrity(void)
{
    block_header_t *current = free_list_head;
    size_t total_size = 0;

    while (current)
    {
        total_size += current->size + sizeof(block_header_t);
        current = current->next;
    }

    if (total_size != HEAP_SIZE)
    {
        print_error("Heap integrity check failed!\n");
        panic("Heap corruption detected");
    }

    print_success("Heap integrity check passed\n");
}

void heap_defragment(void)
{
    // Implementación básica de defragmentación
    // En una implementación real, esto movería bloques ocupados
    print_success("Defragmentation not implemented yet\n");
}

// === FUNCIONES INTERNAS ===

block_header_t *get_free_list_head(void)
{
    return free_list_head;
}

size_t get_free_list_count(void)
{
    size_t count = 0;
    block_header_t *current = free_list_head;

    while (current)
    {
        if (current->is_free)
        {
            count++;
        }
        current = current->next;
    }
    return count;
}

void heap_reset(void)
{
    heap_init();
}

// === COMPATIBILIDAD CON CÓDIGO EXISTENTE ===

// Stubs para variables de memoria que otros archivos esperan
uint32_t free_pages_count = 1000;  // Valor fijo para el scheduler
uint32_t total_pages_count = 1024; // Valor fijo para el scheduler
