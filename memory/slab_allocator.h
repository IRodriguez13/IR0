// ===============================================================================
// IR0 KERNEL - SLAB ALLOCATOR
// ===============================================================================

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ===============================================================================
// SLAB ALLOCATOR STRUCTURES
// ===============================================================================

typedef struct slab
{
    struct slab *next;
    struct slab *prev;
    void *objects;          // Puntero al array de objetos
    uint32_t free_map;      // Bitmap de objetos libres (1 = libre, 0 = ocupado)
    uint32_t inuse_count;   // Número de objetos en uso
    uint32_t total_objects; // Número total de objetos en este slab
} slab_t;

typedef struct slab_cache
{
    const char *name;
    size_t object_size;      // Tamaño de cada objeto
    size_t objects_per_slab; // Objetos por slab
    size_t total_objects;    // Total de objetos en todos los slabs
    size_t free_objects;     // Objetos libres totales

    slab_t *free_slabs;    // Slabs completamente libres
    slab_t *partial_slabs; // Slabs parcialmente ocupados
    slab_t *full_slabs;    // Slabs completamente ocupados

    // Callbacks opcionales
    void (*ctor)(void *obj); // Constructor
    void (*dtor)(void *obj); // Destructor
} slab_cache_t;

// ===============================================================================
// SLAB ALLOCATOR FUNCTIONS
// ===============================================================================

// Crear un nuevo slab cache
slab_cache_t *slab_cache_create(const char *name, size_t object_size,
                                void (*ctor)(void *), void (*dtor)(void *));

// Destruir un slab cache
void slab_cache_destroy(slab_cache_t *cache);

// Asignar un objeto del cache
void *slab_alloc(slab_cache_t *cache);

// Liberar un objeto al cache
void slab_free(slab_cache_t *cache, void *obj);

// Obtener estadísticas del cache
void slab_cache_get_stats(slab_cache_t *cache, size_t *total, size_t *free);

// Mostrar información del cache
void slab_cache_print_info(slab_cache_t *cache);

// ===============================================================================
// SLAB ALLOCATOR GLOBAL FUNCTIONS
// ===============================================================================

// Inicializar el sistema de slab allocator
int slab_allocator_init(void);

// Limpiar el sistema de slab allocator
void slab_allocator_cleanup(void);

// Crear caches predefinidos para tamaños comunes
void slab_create_common_caches(void);

// Obtener cache para un tamaño específico
slab_cache_t *slab_get_cache_for_size(size_t size);

// ===============================================================================
// UTILITY FUNCTIONS
// ===============================================================================

// Calcular el número óptimo de objetos por slab
size_t slab_calculate_objects_per_slab(size_t object_size);

// Verificar si un puntero pertenece a un cache
bool slab_is_valid_ptr(slab_cache_t *cache, void *ptr);

// Obtener el tamaño de un objeto en un cache
size_t slab_get_object_size(slab_cache_t *cache);
