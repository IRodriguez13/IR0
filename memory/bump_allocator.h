#pragma once
#include <stdint.h>
#include <stddef.h>
#include <ir0/panic/panic.h>

// === ESTRUCTURAS DE METADATOS PARA FREE-LIST ===

// Metadatos de bloque de memoria
typedef struct block_header {
    size_t size;                    // Tamaño del bloque (sin incluir header)
    struct block_header *next;      // Siguiente bloque en la lista
    uint8_t is_free;               // 1 = libre, 0 = ocupado
    uint8_t padding[7];            // Padding para alineación 16-byte
} block_header_t;

// Estrategias de búsqueda de bloques
typedef enum {
    FIRST_FIT,      // Primer bloque que quepa
    BEST_FIT,       // Bloque más pequeño que quepa
    WORST_FIT       // Bloque más grande disponible
} allocation_strategy_t;

// === FUNCIONES PRINCIPALES DE MEMORIA ===

// Funciones básicas de memoria
void *kmalloc(size_t size);
void kfree(void *ptr);
void *krealloc(void *ptr, size_t new_size);
void *kcalloc(size_t nmemb, size_t size);

// Funciones avanzadas de memoria
void *kmalloc_aligned(size_t size, size_t alignment);
void kfree_aligned(void *ptr);

// === FUNCIONES DE GESTIÓN DEL HEAP ===

// Inicialización y configuración
void heap_init(void);
void heap_set_strategy(allocation_strategy_t strategy);

// Funciones de estadísticas del heap
size_t get_heap_used(void);
size_t get_heap_free(void);
size_t get_heap_total(void);
size_t get_heap_fragments(void);
size_t get_heap_largest_free_block(void);

// Funciones de debugging y diagnóstico
void heap_dump_info(void);
void heap_validate_integrity(void);
void heap_defragment(void);

// === FUNCIONES INTERNAS (para testing) ===

// Funciones internas para testing y debugging
block_header_t *get_free_list_head(void);
size_t get_free_list_count(void);
void heap_reset(void);

// Variables globales del heap (para debugging)
extern uint8_t *heap_base;
extern uint8_t *heap_end;
extern uint8_t *heap_ptr;
extern block_header_t *free_list_head;
extern allocation_strategy_t current_strategy;
