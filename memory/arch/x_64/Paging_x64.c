#include <stdint.h>
#include <stddef.h>
#include "Paging_x64.h"
#include <string.h>  // Para memset

// Estructuras para paginación x86_64 (4 niveles)
#define PML4_ENTRIES 512
#define PDPT_ENTRIES 512
#define PD_ENTRIES   512
#define PT_ENTRIES   512
#define PAGE_SIZE    4096

// Tablas alineadas a 4KB
__attribute__((aligned(PAGE_SIZE))) uint64_t PML4[PML4_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint64_t PDPT[PDPT_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint64_t PD[PD_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint64_t PT[PT_ENTRIES];

// Flags comunes
#define PAGE_PRESENT (1ULL << 0)
#define PAGE_WRITE   (1ULL << 1)
#define PAGE_USER    (1ULL << 2)
#define PAGE_HUGE    (1ULL << 7)

// Implementación local de memset para evitar dependencias
static void* local_memset(void* ptr, int value, size_t size) 
{
    unsigned char* p = ptr;
    while (size--) 
    {
        *p++ = (unsigned char)value;
    }
    return ptr;
}

void fill_page_table(uint64_t *table, uint64_t start_addr, uint64_t flags) 
{
    for (uint64_t i = 0; i < PT_ENTRIES; i++) 
    {
        table[i] = (start_addr + i * PAGE_SIZE) | flags;
    }
}

void init_paging_x64() 
{
    // 1. Limpiar todas las entradas
    local_memset(PML4, 0, PAGE_SIZE);
    local_memset(PDPT, 0, PAGE_SIZE);
    local_memset(PD, 0, PAGE_SIZE);
    local_memset(PT, 0, PAGE_SIZE);

    // 2. Mapeo identidad de los primeros 2MB (para código/tablas)
    // Nivel PML4
    PML4[0] = ((uint64_t)PDPT) | PAGE_PRESENT | PAGE_WRITE;
    
    // Nivel PDPT
    PDPT[0] = ((uint64_t)PD) | PAGE_PRESENT | PAGE_WRITE;
    
    // Nivel PD (Usando página de 2MB)
    PD[0] = 0x0 | PAGE_PRESENT | PAGE_WRITE | PAGE_HUGE;
    
    // 3. Mapear memoria superior (ej: 0xFFFF800000000000)
    PML4[256] = ((uint64_t)PDPT) | PAGE_PRESENT | PAGE_WRITE;
    
    // 4. Llamar a la función que carga CR3
    paging_set_cpu_x64((uint64_t)PML4);
}

// NUEVA: Implementar la función que faltaba
void paging_set_cpu_x64(uint64_t pml4_addr)
{
    // Cargar PML4 en CR3
    asm volatile(
        "mov %0, %%cr3"
        : 
        : "r"(pml4_addr)
        : "memory"
    );
}