#ifndef IR0_HEAP_ALLOCATOR_H // pragma once es mi convención, pero se ve muy pro usar IR0_HEAP_ALLOCATOR_H.
#define IR0_HEAP_ALLOCATOR_H

#include <stddef.h> // size_t
#include <stdint.h> // uintptr_t, uintXX_t (si es que las necesito)
#include <stdbool.h>

/*
 * Tipos portables
 * - heap_magic_t usa uintptr_t para adaptarse a 32/64 bits sin romper alineaciones.
 * - size_t se usa para tamaños en bytes.
 */
typedef uintptr_t heap_magic_t;

/* Constantes configurables */
#ifndef HEAP_MAGIC
#define HEAP_MAGIC ((heap_magic_t)0xA1B2C3D4A5B6C7D8ULL)
#endif

#ifndef MIN_BLOCK_SIZE
/* tamaño mínimo utilizable para un bloque libre (en bytes) */
#define MIN_BLOCK_SIZE 32
#endif

/* Alineación (power-of-two). Ajusta si necesitás 16/32 bytes por simd/ucontextos) */
#ifndef HEAP_ALIGNMENT
#define HEAP_ALIGNMENT 8
#endif

/* Macro para alinear */
#define ALIGN_UP(x, a) (((x) + ((a) - 1)) & ~((a) - 1))

/*
 * Estructura de bloque: boundary-tag style.
 * - size: tamaño utilizable del bloque (no cuenta metadata).
 * - magic: para detectar corrupción; usamos heap_magic_t portable.
 * - is_free: flag simple.
 * - next/prev: lista doble enlazada para free list / coalescing.
 *
 * NOTA: mantener sizeof(heap_block_t) alineado con HEAP_ALIGNMENT.
 */
typedef struct heap_block
{
    size_t size;
    heap_magic_t magic;
    bool is_free;
    struct heap_block *next;
    struct heap_block *prev;
} heap_block_t;

/* Estadísticas/estado del heap (definir en el .c) */
extern size_t heap_used_bytes;
extern size_t heap_free_bytes;

/* API interna (implementaciones en heap_allocator.c) */

/*
            == heap_allocator_init ==

    Crea el bloque principal de memoria que llamamos heap. Y crea variables globales de control como stats, punteros a regiones específicas, etc.

*/
void heap_allocator_init(void);

/*
            == split_block ==


    Se encarga de dividir ese bloque creado en dos: el solicitado por el kernel/usuario según size_t wanted_size, y el resto de la memoria disponible.
    Actualiza los punteros *next y *prev para que los enlaces se mantengan correctos en el heap allocator.

*/
void split_block(heap_block_t *block, size_t wanted_size);

/*
            == split_block ==

    Verifica que el puntero dado apunte a un bloque válido dentro del heap.

    Sirve para validar punteros antes de realloc o free y evitar corrupciones o accesos inválidos.

*/
bool is_valid_heap_pointer(void *ptr);

/* Wrappers que exponés al resto del kernel (ya tenés prototipos en tu código) */

/*
       === *kmalloc_impl ===
 Implementa la asignación de memoria.

 Busca un bloque libre del tamaño solicitado o mayor.

 Si el bloque es demasiado grande, lo divide (usa split_block).

 Marca el bloque como ocupado y devuelve el puntero a la memoria usable.*/
void *kmalloc_impl(size_t size);

/*
       === kfree_impl ===

Marca un bloque de páginas como libre luego de desocuparlo.
Intenta reasignarlo con bloques adyacentes si puede.

*/
void kfree_impl(void *ptr);

/*
       === debug_heap_allocator ===

    Imprime el estado actual del heap (bloques, tamaños, si están libres o no).

    Muy útil para debugging y ver cómo va evolucionando el heap.

*/
void debug_heap_allocator(void);

/*
       === heap_allocator_cleanup ===

    Limpia el heap dinámico y libera todas las páginas físicas asignadas.
    
    Útil para shutdown del kernel o reinicio del sistema de memoria.

*/
void heap_allocator_cleanup(void);

/*
       === heap_grow ===

    Expande el heap dinámicamente asignando nuevas páginas físicas.
    
    Retorna 0 en éxito, -1 en error.

*/
int heap_grow_public(size_t additional_pages);

/*
       === heap_get_stats ===

    Obtiene estadísticas del heap dinámico.
    
    Retorna información sobre páginas asignadas, memoria usada, etc.

*/
void heap_get_stats(uint32_t *total_pages, uint32_t *used_pages, 
                   size_t *total_bytes, size_t *used_bytes, size_t *free_bytes);

#endif /* IR0_HEAP_ALLOCATOR_H */
