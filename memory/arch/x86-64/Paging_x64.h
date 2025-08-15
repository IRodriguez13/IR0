#ifndef PAGING_X64_H
#define PAGING_X64_H

#include <stdint.h>

// Definición de estructuras de tablas de paginación
typedef uint64_t pml4_entry_t;
typedef uint64_t pdpt_entry_t;
typedef uint64_t pd_entry_t;
typedef uint64_t pt_entry_t;

// Flags para entradas de tablas
#define PAGE_PRESENT    (1ULL << 0)
#define PAGE_WRITE      (1ULL << 1)
#define PAGE_USER       (1ULL << 2)
#define PAGE_WRITETHROUGH (1ULL << 3)
#define PAGE_CACHE_DISABLE (1ULL << 4)
#define PAGE_ACCESSED   (1ULL << 5)
#define PAGE_DIRTY      (1ULL << 6)
#define PAGE_HUGE       (1ULL << 7)       // PS bit para páginas grandes
#define PAGE_GLOBAL     (1ULL << 8)
#define PAGE_NX         (1ULL << 63)      // Bit No-eXecute (solo x86_64)

// Prototipos de funciones
void init_paging_x64(void);
void map_page_x64(uint64_t virt, uint64_t phys, uint64_t flags);
void unmap_page_x64(uint64_t virt);
uint64_t get_phys_addr_x64(uint64_t virt);

// Funciones ASM externas
void paging_set_cpu_x64(uint64_t pml4_addr);
extern void invlpg(uint64_t virt);

// Macros útiles
// #define PAGE_ALIGN(addr) (((addr) + 0xFFF) & ~0xFFF)
// #define IS_PAGE_ALIGNED(addr) (((addr) & 0xFFF) == 0)

// Estructura para representar un rango de memoria
struct memory_range 
{
    uint64_t base;
    uint64_t length;
    uint64_t flags;
};

#endif // PAGING_X64_H