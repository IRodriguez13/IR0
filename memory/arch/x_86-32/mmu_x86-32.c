// memory/arch/x86-32/mmu_x86.c - Implementación específica x86-32
#include "../../memo_interface.h"
#include "Paging_x86-32.h"

// Acceso a las tablas de paginación globales definidas en Paging_x86.c
extern uint32_t page_directory[1024];
extern uint32_t first_page_table[1024];

// Convertir flags comunes a flags x86-32
static uint32_t convert_flags(uint32_t common_flags) 
{
    uint32_t x86_flags = 0;
    
    if (common_flags & PAGE_FLAG_PRESENT)   x86_flags |= PAGE_PRESENT;
    if (common_flags & PAGE_FLAG_WRITABLE)  x86_flags |= PAGE_WRITE;
    if (common_flags & PAGE_FLAG_USER)      x86_flags |= PAGE_USER;
    // x86-32 no tiene NX bit por defecto, ignoramos EXECUTABLE
    
    return x86_flags;
}

int arch_map_page(uintptr_t virt_addr, uintptr_t phys_addr, uint32_t flags) 
{
    // Validaciones
    if (!IS_PAGE_ALIGNED(virt_addr) || !IS_PAGE_ALIGNED(phys_addr)) 
    {
        return -1; // Error: direcciones no alineadas
    }
    
    // Extraer índices de la dirección virtual
    uint32_t pd_index = virt_addr >> 22;        // Bits 31-22: índice en page directory
    uint32_t pt_index = (virt_addr >> 12) & 0x3FF; // Bits 21-12: índice en page table
    
    // Por ahora solo soportamos mapeo en las tablas ya creadas
    if (pd_index >= 10) { // Solo tenemos 10 tablas creadas en init_paging_x86
        return -2; // Error: tabla no existe
    }
    
    // Obtener puntero a la tabla de páginas correcta
    uint32_t* page_table;
    switch (pd_index) 
    {
        case 0: page_table = first_page_table; break;
        // Agregar casos para las otras tablas cuando las necesites
        default: return -3; // Error: tabla no implementada aún
    }
    
    // Mapear la página
    uint32_t x86_flags = convert_flags(flags);
    page_table[pt_index] = phys_addr | x86_flags;
    
    // Invalidar TLB
    arch_invalidate_page(virt_addr);
    
    return 0; // Éxito
}

int arch_unmap_page(uintptr_t virt_addr)
{
    if (!IS_PAGE_ALIGNED(virt_addr)) 
    {
        return -1;
    }
    
    uint32_t pd_index = virt_addr >> 22;
    uint32_t pt_index = (virt_addr >> 12) & 0x3FF;
    
    if (pd_index >= 10) 
    {
        return -2;
    }
    
    uint32_t* page_table;
    switch (pd_index) 
    {
        case 0: page_table = first_page_table; break;
        default: return -3;
    }
    
    // Unmapear (quitar flag PRESENT)
    page_table[pt_index] = 0;
    
    // Invalidar TLB
    arch_invalidate_page(virt_addr);
    
    return 0;
}

uintptr_t arch_virt_to_phys(uintptr_t virt_addr) 
{
    uint32_t pd_index = virt_addr >> 22;
    uint32_t pt_index = (virt_addr >> 12) & 0x3FF;
    uint32_t offset = virt_addr & 0xFFF;
    
    if (pd_index >= 10) 
    {
        return 0; // Error
    }
    
    uint32_t* page_table;
    switch (pd_index) 
    {
        case 0: page_table = first_page_table; break;
        default: return 0;
    }
    
    uint32_t pte = page_table[pt_index];
    if (!(pte & PAGE_PRESENT)) 
    {
        return 0; // Página no mapeada
    }
    
    // Extraer dirección física y agregar offset
    return (pte & ~0xFFF) | offset;
}

void arch_invalidate_page(uintptr_t virt_addr) {
    asm volatile(
        "invlpg (%0)"
        : 
        : "r"(virt_addr)
        : "memory"
    );
}

uintptr_t arch_create_page_directory(void) {
    // Por ahora retornamos el directorio global
    // TODO: Implementar creación de directorios independientes para procesos
    return (uintptr_t)page_directory;
}

void arch_destroy_page_directory(uintptr_t page_dir) {
    // TODO: Implementar cuando tengamos allocator de páginas
    (void)page_dir; // Evitar warning
}


void arch_switch_page_directory(uintptr_t page_dir) {
    asm volatile(
        "mov %0, %%cr3"
        :
        : "r"(page_dir)
        : "memory"
    );
}