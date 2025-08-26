// ===============================================================================
// IR0 KERNEL - SLAB ALLOCATOR IMPLEMENTATION
// ===============================================================================

#include <slab_allocator.h>
#include <bump_allocator.h>
#include <ir0/print.h>
#include <ir0/panic/panic.h>
#include <string.h>

// ===============================================================================
// CONSTANTS AND CONFIGURATION
// ===============================================================================

#define SLAB_HEADER_SIZE sizeof(slab_t)
#define SLAB_ALIGNMENT 8
#define MAX_OBJECTS_PER_SLAB 32
#define SLAB_SIZE (4096) // 4KB por slab

// ===============================================================================
// GLOBAL VARIABLES
// ===============================================================================

static slab_cache_t *common_caches[8] = {NULL}; // Caches para tamaños comunes
static bool slab_allocator_initialized = false;

// ===============================================================================
// UTILITY FUNCTIONS
// ===============================================================================

size_t slab_calculate_objects_per_slab(size_t object_size) {
    if (object_size == 0) return 0;
    
    // Calcular cuántos objetos caben en un slab
    size_t available_space = SLAB_SIZE - SLAB_HEADER_SIZE;
    size_t objects = available_space / object_size;
    
    // Limitar a un máximo razonable
    if (objects > MAX_OBJECTS_PER_SLAB) {
        objects = MAX_OBJECTS_PER_SLAB;
    }
    
    return objects > 0 ? objects : 1;
}

size_t slab_get_object_size(slab_cache_t *cache) {
    return cache ? cache->object_size : 0;
}

bool slab_is_valid_ptr(slab_cache_t *cache, void *ptr) {
    if (!cache || !ptr) return false;
    
    // Buscar en todos los slabs del cache
    slab_t *slab = cache->free_slabs;
    while (slab) {
        if (ptr >= slab->objects && 
            ptr < (char*)slab->objects + (slab->total_objects * cache->object_size)) {
            return true;
        }
        slab = slab->next;
    }
    
    slab = cache->partial_slabs;
    while (slab) {
        if (ptr >= slab->objects && 
            ptr < (char*)slab->objects + (slab->total_objects * cache->object_size)) {
            return true;
        }
        slab = slab->next;
    }
    
    slab = cache->full_slabs;
    while (slab) {
        if (ptr >= slab->objects && 
            ptr < (char*)slab->objects + (slab->total_objects * cache->object_size)) {
            return true;
        }
        slab = slab->next;
    }
    
    return false;
}

// ===============================================================================
// SLAB MANAGEMENT FUNCTIONS
// ===============================================================================

static slab_t *slab_create(slab_cache_t *cache) {
    // Asignar memoria para el slab
    void *slab_memory = kmalloc(SLAB_SIZE);
    if (!slab_memory) {
        return NULL;
    }
    
    // Inicializar el slab
    slab_t *slab = (slab_t *)slab_memory;
    slab->next = NULL;
    slab->prev = NULL;
    slab->objects = (char*)slab_memory + SLAB_HEADER_SIZE;
    slab->free_map = 0xFFFFFFFF; // Todos libres inicialmente
    slab->inuse_count = 0;
    slab->total_objects = cache->objects_per_slab;
    
    // Ajustar el bitmap según el número real de objetos
    if (slab->total_objects < 32) {
        slab->free_map = (1 << slab->total_objects) - 1;
    }
    
    // Llamar constructor si existe
    if (cache->ctor) {
        for (size_t i = 0; i < slab->total_objects; i++) {
            void *obj = (char*)slab->objects + (i * cache->object_size);
            cache->ctor(obj);
        }
    }
    
    return slab;
}

static void slab_destroy(slab_cache_t *cache, slab_t *slab) {
    if (!slab) return;
    
    // Llamar destructor si existe
    if (cache->dtor) {
        for (size_t i = 0; i < slab->total_objects; i++) {
            void *obj = (char*)slab->objects + (i * cache->object_size);
            cache->dtor(obj);
        }
    }
    
    // Liberar memoria del slab
    kfree(slab);
}

static void slab_list_remove(slab_t **list, slab_t *slab) {
    if (!list || !slab) return;
    
    if (slab->prev) {
        slab->prev->next = slab->next;
    } else {
        *list = slab->next;
    }
    
    if (slab->next) {
        slab->next->prev = slab->prev;
    }
}

static void slab_list_add(slab_t **list, slab_t *slab) {
    if (!list || !slab) return;
    
    slab->next = *list;
    slab->prev = NULL;
    if (*list) {
        (*list)->prev = slab;
    }
    *list = slab;
}

// ===============================================================================
// SLAB CACHE FUNCTIONS
// ===============================================================================

slab_cache_t *slab_cache_create(const char *name, size_t object_size,
                               void (*ctor)(void *), void (*dtor)(void *)) {
    if (!name || object_size == 0) {
        return NULL;
    }
    
    // Crear el cache
    slab_cache_t *cache = kmalloc(sizeof(slab_cache_t));
    if (!cache) {
        return NULL;
    }
    
    // Inicializar el cache
    cache->name = name;
    cache->object_size = object_size;
    cache->objects_per_slab = slab_calculate_objects_per_slab(object_size);
    cache->total_objects = 0;
    cache->free_objects = 0;
    cache->free_slabs = NULL;
    cache->partial_slabs = NULL;
    cache->full_slabs = NULL;
    cache->ctor = ctor;
    cache->dtor = dtor;
    
    return cache;
}

void slab_cache_destroy(slab_cache_t *cache) {
    if (!cache) return;
    
    // Destruir todos los slabs
    slab_t *slab = cache->free_slabs;
    while (slab) {
        slab_t *next = slab->next;
        slab_destroy(cache, slab);
        slab = next;
    }
    
    slab = cache->partial_slabs;
    while (slab) {
        slab_t *next = slab->next;
        slab_destroy(cache, slab);
        slab = next;
    }
    
    slab = cache->full_slabs;
    while (slab) {
        slab_t *next = slab->next;
        slab_destroy(cache, slab);
        slab = next;
    }
    
    kfree(cache);
}

void *slab_alloc(slab_cache_t *cache) {
    if (!cache) return NULL;
    
    slab_t *slab = NULL;
    
    // Buscar en slabs parcialmente ocupados
    if (cache->partial_slabs) {
        slab = cache->partial_slabs;
    }
    // Si no hay parciales, buscar en slabs libres
    else if (cache->free_slabs) {
        slab = cache->free_slabs;
        slab_list_remove(&cache->free_slabs, slab);
        slab_list_add(&cache->partial_slabs, slab);
    }
    // Si no hay slabs disponibles, crear uno nuevo
    else {
        slab = slab_create(cache);
        if (!slab) {
            return NULL;
        }
        slab_list_add(&cache->partial_slabs, slab);
        cache->total_objects += slab->total_objects;
        cache->free_objects += slab->total_objects;
    }
    
    // Encontrar el primer bit libre en el bitmap
    uint32_t bit = 0;
    while (bit < 32 && !(slab->free_map & (1 << bit))) {
        bit++;
    }
    
    if (bit >= slab->total_objects) {
        // No debería pasar, pero por seguridad
        return NULL;
    }
    
    // Marcar como ocupado
    slab->free_map &= ~(1 << bit);
    slab->inuse_count++;
    cache->free_objects--;
    
    // Si el slab está lleno, moverlo a la lista de llenos
    if (slab->inuse_count == slab->total_objects) {
        slab_list_remove(&cache->partial_slabs, slab);
        slab_list_add(&cache->full_slabs, slab);
    }
    
    // Retornar el objeto
    return (char*)slab->objects + (bit * cache->object_size);
}

void slab_free(slab_cache_t *cache, void *obj) {
    if (!cache || !obj) return;
    
    // Encontrar el slab que contiene este objeto
    slab_t *slab = cache->free_slabs;
    while (slab) {
        if (obj >= slab->objects && 
            obj < (char*)slab->objects + (slab->total_objects * cache->object_size)) {
            break;
        }
        slab = slab->next;
    }
    
    if (!slab) {
        slab = cache->partial_slabs;
        while (slab) {
            if (obj >= slab->objects && 
                obj < (char*)slab->objects + (slab->total_objects * cache->object_size)) {
                break;
            }
            slab = slab->next;
        }
    }
    
    if (!slab) {
        slab = cache->full_slabs;
        while (slab) {
            if (obj >= slab->objects && 
                obj < (char*)slab->objects + (slab->total_objects * cache->object_size)) {
                break;
            }
            slab = slab->next;
        }
    }
    
    if (!slab) {
        // Objeto no encontrado en ningún slab
        return;
    }
    
    // Calcular el índice del objeto
    size_t offset = (char*)obj - (char*)slab->objects;
    size_t index = offset / cache->object_size;
    
    if (index >= slab->total_objects) {
        return;
    }
    
    // Marcar como libre
    slab->free_map |= (1 << index);
    slab->inuse_count--;
    cache->free_objects++;
    
    // Si el slab estaba lleno, moverlo a parciales
    if (slab->inuse_count == slab->total_objects - 1) {
        slab_list_remove(&cache->full_slabs, slab);
        slab_list_add(&cache->partial_slabs, slab);
    }
    // Si el slab está completamente libre, moverlo a libres
    else if (slab->inuse_count == 0) {
        slab_list_remove(&cache->partial_slabs, slab);
        slab_list_add(&cache->free_slabs, slab);
    }
}

void slab_cache_get_stats(slab_cache_t *cache, size_t *total, size_t *free) {
    if (total) *total = cache ? cache->total_objects : 0;
    if (free) *free = cache ? cache->free_objects : 0;
}

void slab_cache_print_info(slab_cache_t *cache) {
    if (!cache) {
        print("Cache is NULL\n");
        return;
    }
    
    print("Cache: ");
    print(cache->name);
    print("\n");
    print("Object size: ");
    print_uint64(cache->object_size);
    print(" bytes\n");
    print("Objects per slab: ");
    print_uint64(cache->objects_per_slab);
    print("\n");
    print("Total objects: ");
    print_uint64(cache->total_objects);
    print("\n");
    print("Free objects: ");
    print_uint64(cache->free_objects);
    print("\n");
}

// ===============================================================================
// GLOBAL SLAB ALLOCATOR FUNCTIONS
// ===============================================================================

int slab_allocator_init(void) {
    if (slab_allocator_initialized) {
        return 0;
    }
    
    print("Initializing slab allocator...\n");
    
    // Crear caches comunes
    slab_create_common_caches();
    
    slab_allocator_initialized = true;
    print("Slab allocator initialized successfully\n");
    
    return 0;
}

void slab_allocator_cleanup(void) {
    if (!slab_allocator_initialized) {
        return;
    }
    
    // Destruir caches comunes
    for (int i = 0; i < 8; i++) {
        if (common_caches[i]) {
            slab_cache_destroy(common_caches[i]);
            common_caches[i] = NULL;
        }
    }
    
    slab_allocator_initialized = false;
    print("Slab allocator cleaned up\n");
}

void slab_create_common_caches(void) {
    // Crear caches para tamaños comunes
    common_caches[0] = slab_cache_create("8-byte", 8, NULL, NULL);
    common_caches[1] = slab_cache_create("16-byte", 16, NULL, NULL);
    common_caches[2] = slab_cache_create("32-byte", 32, NULL, NULL);
    common_caches[3] = slab_cache_create("64-byte", 64, NULL, NULL);
    common_caches[4] = slab_cache_create("128-byte", 128, NULL, NULL);
    common_caches[5] = slab_cache_create("256-byte", 256, NULL, NULL);
    common_caches[6] = slab_cache_create("512-byte", 512, NULL, NULL);
    common_caches[7] = slab_cache_create("1024-byte", 1024, NULL, NULL);
    
    print("Common slab caches created\n");
}

slab_cache_t *slab_get_cache_for_size(size_t size) {
    // Buscar el cache más apropiado para el tamaño
    for (int i = 0; i < 8; i++) {
        if (common_caches[i] && common_caches[i]->object_size >= size) {
            return common_caches[i];
        }
    }
    
    return NULL;
}
