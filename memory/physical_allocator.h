#ifndef IR0_PHYSCAL_ALLOCATOR
#define IR0_PHYSCAL_ALLOCATOR
#include <stdint.h>



static inline void set_page_free(uintptr_t phys_addr);


static inline int is_page_used(uintptr_t phys_addr);


void physical_allocator_init(void);


/*
        === alloc_physical_page ===
   Busca usando un bitmap páginas libres y, si las encuentra, las marca como usadas 
*/
uintptr_t alloc_physical_page(void);

/*
        === free_physical_page ===
    Si está en rangos válidos de memoria física, está el allocator iniciado, etc. Llama a set_page_free para liberar ese bloque de páginas.
*/
void free_physical_page(uintptr_t phys_addr);

/*
        === debug_physical_allocator ===
    Básicamente me da un reporte del estado de la paginación con datos como
    dónde inicia y termina la memo física, cuántas páginas tengo mapeadas, cuántes libres, porcentaje de uso de la memoria virtual mapeada.
*/
void debug_physical_allocator(void);

#endif

