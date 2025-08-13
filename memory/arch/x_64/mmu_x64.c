// memory/arch/x_64/mmu_x64.c - MMU para x86-64
#include "../../memo_interface.h"
#include "Paging_x64.h"
#include <print.h>

// Acceso a las tablas de paginación globales definidas en Paging_x64.c
extern uint64_t PML4[512];
extern uint64_t PDPT[512];
extern uint64_t PD[512];
extern uint64_t PT[512];

// Convertir flags comunes a flags x86-64
static uint64_t convert_flags_x64(uint32_t common_flags)
{
    uint64_t x64_flags = 0;

    if (common_flags & PAGE_FLAG_PRESENT)
        x64_flags |= PAGE_PRESENT;
    if (common_flags & PAGE_FLAG_WRITABLE)
        x64_flags |= PAGE_WRITE;
    if (common_flags & PAGE_FLAG_USER)
        x64_flags |= PAGE_USER;

    // En x86-64 podemos usar NX bit si queremos
    if (!(common_flags & PAGE_FLAG_EXECUTABLE))
    {
        x64_flags |= PAGE_NX; // No-eXecute bit (bit 63)
    }

    return x64_flags;
}

int arch_map_page(uintptr_t virt_addr, uintptr_t phys_addr, uint32_t flags)
{
    // Validaciones
    if (!IS_PAGE_ALIGNED(virt_addr) || !IS_PAGE_ALIGNED(phys_addr))
    {
        LOG_ERR("arch_map_page: Direcciones no alineadas");
        return -1;
    }

    // Extraer índices de la dirección virtual (x86-64: 4 niveles)
    uint64_t pml4_index = (virt_addr >> 39) & 0x1FF; // Bits 47-39
    uint64_t pdpt_index = (virt_addr >> 30) & 0x1FF; // Bits 38-30
    uint64_t pd_index = (virt_addr >> 21) & 0x1FF;   // Bits 29-21
    uint64_t pt_index = (virt_addr >> 12) & 0x1FF;   // Bits 20-12

    // Por ahora solo mapear en las primeras entradas (mapeo básico)
    if (pml4_index >= 1 || pdpt_index >= 1 || pd_index >= 1)
    {
        LOG_ERR("arch_map_page: Índices fuera del rango implementado");
        return -2;
    }

    // Verificar que las tablas superiores estén correctamente configuradas
    if (!(PML4[pml4_index] & PAGE_PRESENT))
    {
        LOG_ERR("arch_map_page: PML4 entry not present");
        return -3;
    }

    if (!(PDPT[pdpt_index] & PAGE_PRESENT))
    {
        LOG_ERR("arch_map_page: PDPT entry not present");
        return -4;
    }

    // Si PD está configurado para páginas de 2MB (huge pages), no podemos mapear 4KB
    if (PD[pd_index] & PAGE_HUGE)
    {
        LOG_ERR("arch_map_page: Cannot map 4KB page in 2MB huge page area");
        return -5;
    }

    // Por ahora, simplificamos usando el mapeo identidad ya creado
    // En una implementación completa, necesitarías crear/gestionar tablas dinámicamente

    LOG_WARN("arch_map_page x64: Mapeo básico no implementado completamente");
    return -99; // Not implemented yet
}

int arch_unmap_page(uintptr_t virt_addr)
{
    if (!IS_PAGE_ALIGNED(virt_addr))
    {
        return -1;
    }

    LOG_WARN("arch_unmap_page x64: No implementado aún");
    return -99;
}

uintptr_t arch_virt_to_phys(uintptr_t virt_addr)
{
    // Para el mapeo identidad actual (primeros 2MB), virtual == física
    if (virt_addr < 0x200000)
    { // Primeros 2MB
        return virt_addr;
    }

    LOG_WARN("arch_virt_to_phys x64: Solo mapeo identidad implementado");
    return 0; // Error para direcciones fuera del mapeo identidad
}

void arch_invalidate_page(uintptr_t virt_addr)
{
    asm volatile(
        "invlpg (%0)"
        :
        : "r"(virt_addr)
        : "memory");
}

uintptr_t arch_create_page_directory(void)
{
    // Por ahora retornamos el PML4 global
    // TODO: Implementar creación de PML4 independientes para procesos
    return (uintptr_t)PML4;
}

void arch_destroy_page_directory(uintptr_t page_dir)
{
    // TODO: Implementar cuando tengamos allocator de páginas para x64
    (void)page_dir; // Evitar warning
}

void arch_switch_page_directory(uintptr_t page_dir)
{
    asm volatile(
        "mov %0, %%cr3"
        :
        : "r"(page_dir)
        : "memory");
}