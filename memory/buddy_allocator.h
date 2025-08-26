// ===============================================================================
// IR0 KERNEL - BUDDY ALLOCATOR
// ===============================================================================

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ===============================================================================
// BUDDY ALLOCATOR STRUCTURES
// ===============================================================================

typedef struct buddy_block {
    struct buddy_block *next;
    uint32_t order;        // Orden del bloque (2^order = tamaño)
    bool is_free;          // Si el bloque está libre
    uintptr_t start_addr;  // Dirección de inicio del bloque
} buddy_block_t;

typedef struct buddy_allocator {
    uintptr_t start_addr;      // Dirección de inicio del área
    size_t total_size;         // Tamaño total del área
    uint32_t max_order;        // Orden máximo (2^max_order = tamaño máximo)
    buddy_block_t *free_lists[32]; // Listas de bloques libres por orden
    uint8_t *bitmap;           // Bitmap para tracking de bloques
} buddy_allocator_t;

// ===============================================================================
// BUDDY ALLOCATOR FUNCTIONS
// ===============================================================================

// Crear un nuevo buddy allocator
buddy_allocator_t *buddy_allocator_create(uintptr_t start_addr, size_t size);

// Destruir un buddy allocator
void buddy_allocator_destroy(buddy_allocator_t *buddy);

// Asignar un bloque de memoria
void *buddy_alloc(buddy_allocator_t *buddy, size_t size);

// Liberar un bloque de memoria
void buddy_free(buddy_allocator_t *buddy, void *ptr);

// Obtener estadísticas del buddy allocator
void buddy_get_stats(buddy_allocator_t *buddy, size_t *total, size_t *free);

// Mostrar información del buddy allocator
void buddy_print_info(buddy_allocator_t *buddy);

// ===============================================================================
// UTILITY FUNCTIONS
// ===============================================================================

// Calcular el orden necesario para un tamaño
uint32_t buddy_get_order(size_t size);

// Calcular el tamaño de un bloque dado su orden
size_t buddy_get_block_size(uint32_t order);

// Verificar si un puntero pertenece al buddy allocator
bool buddy_is_valid_ptr(buddy_allocator_t *buddy, void *ptr);

// Obtener el tamaño de un bloque asignado
size_t buddy_get_allocated_size(buddy_allocator_t *buddy, void *ptr);

// ===============================================================================
// GLOBAL BUDDY ALLOCATOR
// ===============================================================================

// Inicializar el buddy allocator global
int buddy_allocator_init(void);

// Limpiar el buddy allocator global
void buddy_allocator_cleanup(void);

// Obtener el buddy allocator global
buddy_allocator_t *buddy_get_global_allocator(void);
