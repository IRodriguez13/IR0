// memory/arch/x86-64/mmu_x64.c - MMU para x86-64
#include "../../memo_interface.h"
#include "Paging_x64.h"
#include <print.h>

// Constantes adicionales
#ifndef PAGE_HUGE
#define PAGE_HUGE (1ULL << 7) // PS bit para páginas grandes
#endif

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
    if (!IS_PAGE_ALIGNED(virt_addr) || !IS_PAGE_ALIGNED(phys_addr))
    {
        return -1;
    }

    // Calcular índices de las tablas de paginación
    uint64_t pml4_index = (virt_addr >> 39) & 0x1FF;
    uint64_t pdpt_index = (virt_addr >> 30) & 0x1FF;
    uint64_t pd_index = (virt_addr >> 21) & 0x1FF;
    uint64_t pt_index = (virt_addr >> 12) & 0x1FF;

    // Convertir flags comunes a flags x86-64
    uint64_t x64_flags = convert_flags_x64(flags);

    // Para el mapeo básico, usar el mapeo identidad ya configurado
    // Si la dirección está en el rango 0-2MB, ya está mapeada
    if (virt_addr < 0x200000 && virt_addr == phys_addr)
    {
        // Ya está mapeado en el mapeo identidad
        return 0;
    }

    // Mapeo especial para el heap del kernel (0x04000000 - 0x06000000)
    // Usamos páginas de 2MB para simplificar
    if (virt_addr >= 0x04000000 && virt_addr < 0x06000000)
    {
        // Calcular índice en PD para esta dirección
        uint64_t heap_pd_index = (virt_addr - 0x04000000) / 0x200000; // Páginas de 2MB
        uint64_t heap_base_phys = phys_addr & ~0x1FFFFF;              // Alinear a 2MB

        // Verificar que PML4[0] y PDPT[0] estén configurados
        if (!(PML4[0] & PAGE_PRESENT))
        {
            PML4[0] = ((uint64_t)PDPT) | PAGE_PRESENT | PAGE_WRITE;
        }
        if (!(PDPT[0] & PAGE_PRESENT))
        {
            PDPT[0] = ((uint64_t)PD) | PAGE_PRESENT | PAGE_WRITE;
        }

        // Mapear usando página de 2MB
        uint64_t pd_heap_index = 32 + heap_pd_index; // Empezar desde PD[32]
        if (pd_heap_index < 512)
        {
            PD[pd_heap_index] = heap_base_phys | x64_flags | PAGE_HUGE;
            arch_invalidate_page(virt_addr);
            return 0;
        }
    }

    // Para direcciones fuera del mapeo identidad, usar el mapeo superior
    if (pml4_index == 1) // Mapeo superior (0x8000000000000000)
    {
        // Verificar que PML4[1] esté configurado
        if (!(PML4[1] & PAGE_PRESENT))
        {
            // Configurar PML4[1] para apuntar a PDPT
            PML4[1] = ((uint64_t)PDPT) | PAGE_PRESENT | PAGE_WRITE;
        }

        // Para el mapeo básico, usar páginas de 2MB (huge pages)
        if (pd_index < 128) // Primeras 128 entradas de PD
        {
            uint64_t huge_page_addr = pd_index * 0x200000; // 2MB por entrada
            PD[pd_index] = huge_page_addr | x64_flags | PAGE_HUGE;

            // Invalidar TLB para esta página
            arch_invalidate_page(virt_addr);
            return 0;
        }
    }

    // Implementación básica para mapeo de páginas de 4KB
    if (pml4_index == 0) // Mapeo inferior (0x0000000000000000)
    {
        // Verificar que PML4[0] esté configurado
        if (!(PML4[0] & PAGE_PRESENT))
        {
            // Configurar PML4[0] para apuntar a PDPT
            PML4[0] = ((uint64_t)PDPT) | PAGE_PRESENT | PAGE_WRITE;
        }

        // Verificar que PDPT[pdpt_index] esté configurado
        if (!(PDPT[pdpt_index] & PAGE_PRESENT))
        {
            // Configurar PDPT[pdpt_index] para apuntar a PD
            PDPT[pdpt_index] = ((uint64_t)PD) | PAGE_PRESENT | PAGE_WRITE;
        }

        // Para páginas de 4KB, necesitamos configurar la tabla de páginas
        if (pd_index < 512)
        {
            // Verificar que PD[pd_index] esté configurado para tabla de páginas
            if (!(PD[pd_index] & PAGE_PRESENT))
            {
                // Crear nueva tabla de páginas si es necesario
                // Por ahora, usar la tabla PT global
                PD[pd_index] = ((uint64_t)PT) | PAGE_PRESENT | PAGE_WRITE;
            }

            // Mapear la página específica
            PT[pt_index] = phys_addr | x64_flags;

            // Invalidar TLB
            arch_invalidate_page(virt_addr);
            return 0;
        }
    }

    LOG_WARN("arch_map_page x64: Dirección fuera del rango soportado");
    return -1; // Dirección fuera del rango soportado
}

int arch_unmap_page(uintptr_t virt_addr)
{
    if (!IS_PAGE_ALIGNED(virt_addr))
    {
        return -1;
    }

    // Calcular índices
    uint64_t pml4_index = (virt_addr >> 39) & 0x1FF;
    uint64_t pd_index = (virt_addr >> 21) & 0x1FF;
    uint64_t pt_index = (virt_addr >> 12) & 0x1FF;

    // Para páginas de 2MB (huge pages)
    if (pml4_index == 1 && pd_index < 128)
    {
        // No podemos unmapear páginas de 2MB estáticas por ahora
        LOG_WARN("arch_unmap_page x64: No se pueden unmapear páginas de 2MB estáticas");
        return -1;
    }

    // Para páginas de 4KB
    if (pml4_index == 0 && pd_index < 512)
    {
        // Verificar que la página esté mapeada
        if (PD[pd_index] & PAGE_PRESENT)
        {
            // Unmapear la página específica
            PT[pt_index] = 0;

            // Invalidar TLB
            arch_invalidate_page(virt_addr);
            return 0;
        }
    }

    LOG_WARN("arch_unmap_page x64: Página no mapeada o fuera del rango soportado");
    return -1;
}

uintptr_t arch_virt_to_phys(uintptr_t virt_addr)
{
    // Para el mapeo identidad actual (primeros 2MB), virtual == física
    if (virt_addr < 0x200000)
    { // Primeros 2MB
        return virt_addr;
    }

    // Para el mapeo superior (0x8000000000000000), calcular offset
    if (virt_addr >= 0x8000000000000000)
    {
        uint64_t offset = virt_addr - 0x8000000000000000;
        if (offset < 0x10000000) // 256MB mapeados
        {
            return offset;
        }
    }

    LOG_WARN("arch_virt_to_phys x64: Dirección fuera del mapeo conocido");
    return 0; // Error para direcciones fuera del mapeo conocido
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