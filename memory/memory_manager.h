// ===============================================================================
// IR0 KERNEL - MODULAR MEMORY MANAGEMENT SYSTEM
// ===============================================================================

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ===============================================================================
// MEMORY ZONE TYPES
// ===============================================================================

typedef enum {
    MEMORY_ZONE_DMA = 0,      // DMA-capable memory (0-16MB)
    MEMORY_ZONE_NORMAL,       // Normal memory (16MB-896MB)
    MEMORY_ZONE_HIGHMEM,      // High memory (>896MB)
    MEMORY_ZONE_COUNT
} memory_zone_type_t;

// ===============================================================================
// ALLOCATOR TYPES
// ===============================================================================

typedef enum {
    ALLOCATOR_BUMP,           // Simple bump allocator
    ALLOCATOR_SLAB,           // Slab allocator for small objects
    ALLOCATOR_BUDDY,          // Buddy system for power-of-2 sizes
    ALLOCATOR_POOL,           // Fixed-size pool allocator
    ALLOCATOR_COUNT
} allocator_type_t;

// ===============================================================================
// MEMORY ALLOCATOR INTERFACE
// ===============================================================================

typedef struct memory_allocator {
    const char *name;
    allocator_type_t type;
    
    // Core allocation functions
    void *(*alloc)(struct memory_allocator *allocator, size_t size);
    void (*free)(struct memory_allocator *allocator, void *ptr);
    void *(*realloc)(struct memory_allocator *allocator, void *ptr, size_t new_size);
    
    // Optional functions
    size_t (*get_allocated_size)(struct memory_allocator *allocator, void *ptr);
    bool (*is_valid_ptr)(struct memory_allocator *allocator, void *ptr);
    void (*defragment)(struct memory_allocator *allocator);
    
    // Statistics
    size_t total_allocated;
    size_t total_freed;
    size_t current_usage;
    size_t peak_usage;
    
    // Private data
    void *private_data;
} memory_allocator_t;

// ===============================================================================
// MEMORY ZONE STRUCTURE
// ===============================================================================

typedef struct memory_zone {
    memory_zone_type_t type;
    uintptr_t start_addr;
    uintptr_t end_addr;
    size_t total_size;
    size_t free_size;
    
    // Allocators for this zone
    memory_allocator_t *primary_allocator;
    memory_allocator_t *slab_allocator;
    memory_allocator_t *buddy_allocator;
    
    // Zone-specific data
    void *zone_data;
} memory_zone_t;

// ===============================================================================
// SLAB ALLOCATOR STRUCTURES
// ===============================================================================

typedef struct slab_cache {
    const char *name;
    size_t object_size;
    size_t objects_per_slab;
    size_t total_objects;
    size_t free_objects;
    
    struct slab *free_slabs;
    struct slab *partial_slabs;
    struct slab *full_slabs;
    
    // Cache-specific functions
    void (*ctor)(void *obj);
    void (*dtor)(void *obj);
} slab_cache_t;

typedef struct slab {
    struct slab *next;
    struct slab *prev;
    slab_cache_t *cache;
    void *objects;
    uint32_t free_map;
    uint32_t inuse_count;
} slab_t;

// ===============================================================================
// BUDDY SYSTEM STRUCTURES
// ===============================================================================

typedef struct buddy_block {
    struct buddy_block *next;
    uint32_t order;
    bool is_free;
} buddy_block_t;

typedef struct buddy_allocator {
    uintptr_t start_addr;
    size_t total_size;
    uint32_t max_order;
    buddy_block_t *free_lists[32];  // One list per order
} buddy_allocator_t;

// ===============================================================================
// MEMORY MANAGER MAIN STRUCTURE
// ===============================================================================

typedef struct memory_manager {
    memory_zone_t zones[MEMORY_ZONE_COUNT];
    memory_allocator_t *default_allocator;
    
    // Global statistics
    size_t total_memory;
    size_t used_memory;
    size_t free_memory;
    
    // Configuration
    bool enable_slabs;
    bool enable_buddy;
    bool enable_debug;
    
    // Debug and monitoring
    void (*debug_callback)(const char *msg);
    void (*error_callback)(const char *msg);
} memory_manager_t;

// ===============================================================================
// CORE MEMORY MANAGER FUNCTIONS
// ===============================================================================

// Initialization
int memory_manager_init(void);
void memory_manager_shutdown(void);

// Zone management
memory_zone_t *memory_get_zone(memory_zone_type_t type);
memory_zone_t *memory_get_zone_for_addr(uintptr_t addr);

// Allocation functions
void *memory_alloc(size_t size);
void *memory_alloc_aligned(size_t size, size_t alignment);
void memory_free(void *ptr);
void *memory_realloc(void *ptr, size_t new_size);
void *memory_calloc(size_t nmemb, size_t size);

// Zone-specific allocation
void *memory_alloc_in_zone(memory_zone_t *zone, size_t size);
void memory_free_in_zone(memory_zone_t *zone, void *ptr);

// ===============================================================================
// SLAB ALLOCATOR FUNCTIONS
// ===============================================================================

// Slab cache management
slab_cache_t *slab_cache_create(const char *name, size_t object_size, 
                               void (*ctor)(void *), void (*dtor)(void *));
void slab_cache_destroy(slab_cache_t *cache);

// Slab allocation
void *slab_alloc(slab_cache_t *cache);
void slab_free(slab_cache_t *cache, void *obj);

// Slab statistics
size_t slab_cache_get_stats(slab_cache_t *cache, size_t *total, size_t *free);

// ===============================================================================
// BUDDY SYSTEM FUNCTIONS
// ===============================================================================

// Buddy allocator management
buddy_allocator_t *buddy_allocator_create(uintptr_t start_addr, size_t size);
void buddy_allocator_destroy(buddy_allocator_t *buddy);

// Buddy allocation
void *buddy_alloc(buddy_allocator_t *buddy, size_t size);
void buddy_free(buddy_allocator_t *buddy, void *ptr);

// Buddy utilities
uint32_t buddy_get_order(size_t size);
size_t buddy_get_block_size(uint32_t order);

// ===============================================================================
// DEBUG AND MONITORING FUNCTIONS
// ===============================================================================

// Memory statistics
void memory_print_stats(void);
void memory_print_zone_stats(memory_zone_t *zone);
void memory_print_allocator_stats(memory_allocator_t *allocator);

// Memory validation
bool memory_validate_ptr(void *ptr);
void memory_validate_heap(void);
void memory_dump_heap(void);

// Memory profiling
void memory_start_profiling(void);
void memory_stop_profiling(void);
void memory_print_profile(void);

// ===============================================================================
// CONFIGURATION FUNCTIONS
// ===============================================================================

// Allocator selection
void memory_set_default_allocator(allocator_type_t type);
void memory_set_zone_allocator(memory_zone_type_t zone, allocator_type_t type);

// Feature toggles
void memory_enable_slabs(bool enable);
void memory_enable_buddy(bool enable);
void memory_enable_debug(bool enable);

// Callbacks
void memory_set_debug_callback(void (*callback)(const char *msg));
void memory_set_error_callback(void (*callback)(const char *msg));

// ===============================================================================
// UTILITY FUNCTIONS
// ===============================================================================

// Size utilities
size_t memory_align_size(size_t size, size_t alignment);
uintptr_t memory_align_addr(uintptr_t addr, size_t alignment);

// Memory operations
void memory_zero(void *ptr, size_t size);
void memory_copy(void *dest, const void *src, size_t size);
void memory_set(void *ptr, int value, size_t size);

// ===============================================================================
// GLOBAL VARIABLES
// ===============================================================================

extern memory_manager_t *g_memory_manager;
