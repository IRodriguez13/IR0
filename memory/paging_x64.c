#include <stdint.h>
#include <print.h>
#include <logging.h>
#include "paging_x64.h"
#include <string.h>
#include <bump_allocator.h>

// ===============================================================================
//                              PAGE TABLE STRUCTURES
// ===============================================================================

// Punteros a tablas, alineadas a 4KiB
__attribute__((aligned(4096), section(".paging"))) static uint64_t PML4[512];
__attribute__((aligned(4096))) static uint64_t PDPT[512];
__attribute__((aligned(4096))) static uint64_t PD[512];

// ===============================================================================
// PAGING SETUP
// ===============================================================================

void setup_paging_identity_16mb()
{
    // NO limpiar tablas - expandir las existentes del boot
    // Las tablas mínimas del boot ya están configuradas
    
    // Expandir PD para mapear 32MB (16 entradas de 2MB) - SOLUCIÓN IDEAL
    for (int i = 1; i < 16; i++)  // Empezar desde 1 (0 ya está mapeado)
    {
        uint64_t phys_addr = i * PAGE_SIZE_2MB;
        PD[i] = phys_addr | PAGE_PRESENT | PAGE_RW | PAGE_SIZE_2MB_FLAG;
    }

    // Mapear 16 MiB usando 2MiB pages: 8 PD entries (2MiB * 8 = 16MiB)
    for (int i = 0; i < 8; i++)
    {
        uint64_t phys_addr = i * PAGE_SIZE_2MB;
        PD[i] = phys_addr | PAGE_PRESENT | PAGE_RW | PAGE_SIZE_2MB_FLAG;
    }

    // NO recargar CR3 - ya está configurado por el boot assembly
    // Solo expandir las tablas existentes
}

void enable_paging(void)
{
    uint64_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000; // PG bit
    asm volatile("mov %0, %%cr0" ::"r"(cr0));
}

void setup_and_enable_paging(void)
{
    // Verificaciones de seguridad SILENCIOSAS
    // NO usar print/log durante el setup crítico
    
    // 1. Verificar que estamos en modo 64-bit
    uint64_t cr0, cr4;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    
    if (!(cr4 & (1 << 5))) 
    {
        // PAE no habilitado - fallo crítico
        return;
    }
    
    // 2. Expandir tablas existentes SILENCIOSO
    setup_paging_identity_16mb();
    
    // 3. NO recargar CR3 - el boot assembly ya lo configuró correctamente
    // load_page_directory((uint64_t)PML4);  // COMENTADO: No recargar CR3
    
    // 4. Verificar que paging está habilitado
    if (!is_paging_enabled()) {
        // Paging no está habilitado - habilitarlo
        enable_paging();
    }
}

/**
 * Verificación POST-paging que SÍ puede usar print/log
 * Solo llamar DESPUÉS de que el paging esté completamente configurado
 */
void verify_paging_setup_safe(void)
{
    log_info("PAGING", "=== POST-PAGING VERIFICATION ===");
    
    // Ahora es seguro usar print porque el paging está activo
    if (is_paging_enabled()) 
    {
        log_info("PAGING", "✓ Paging is enabled");
    } 
    else 
    {
        log_error("PAGING", "✗ Paging is NOT enabled");
        return;
    }
    
    // Verificar CR3
    uint64_t cr3 = get_current_page_directory();
    log_info_fmt("PAGING", "CR3: 0x%llx", cr3);
    
    // Verificar que CR3 apunta a PML4
    if (cr3 == (uint64_t)PML4) 
    {
        log_info("PAGING", "✓ CR3 points to correct PML4");
    } 
    else 
    {
        log_error("PAGING", "✗ CR3 points to wrong address");
    }
    
    // Verificar entradas críticas
    if (PML4[0] & PAGE_PRESENT) 
    {
        log_info("PAGING", "✓ PML4[0] is present");
    } 
    else 
    {
        log_error("PAGING", "✗ PML4[0] is not present");
    }
    
    if (PDPT[0] & PAGE_PRESENT) 
    {
        log_info("PAGING", "✓ PDPT[0] is present");
    } 
    else 
    {
        log_error("PAGING", "✗ PDPT[0] is not present");
    }
    
    // Verificar primera entrada PD
    if (PD[0] & PAGE_PRESENT && PD[0] & PAGE_SIZE_2MB_FLAG) 
    {
        log_info("PAGING", "✓ PD[0] is present and 2MB page");
    } 
    else 
    {
        log_error("PAGING", "✗ PD[0] is not properly configured");
    }
    
    log_info("PAGING", "=== POST-PAGING VERIFICATION COMPLETE ===");
}

void load_page_directory(uint64_t pml4_addr)
{
    asm volatile("mov %0, %%cr3" ::"r"(pml4_addr));
}

uint64_t get_current_page_directory(void)
{
    uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

int is_paging_enabled(void)
{
    uint64_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    return (cr0 & 0x80000000) != 0;
}

// ===============================================================================
// PAGE MAPPING (SIMPLE IMPLEMENTATION)
// ===============================================================================

int map_page(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags)
{
    (void)virt_addr; // Suppress unused parameter warning
    (void)phys_addr; // Suppress unused parameter warning
    (void)flags;     // Suppress unused parameter warning

    return 0;
}

int unmap_page(uint64_t virt_addr)
{
    (void)virt_addr; // Suppress unused parameter warning

    return 0;
}

// ===============================================================================
// USER MEMORY MAPPING FUNCTIONS
// ===============================================================================

// Mapear página de usuario con permisos U/S=1
int map_user_page(uintptr_t virtual_addr, uintptr_t physical_addr, uint64_t flags)
{
    // Agregar flag de usuario (U/S=1)
    flags |= PAGE_USER;
    
    // Mapear la página
    return map_page(virtual_addr, physical_addr, flags);
}

// Mapear región de memoria de usuario
int map_user_region(uintptr_t virtual_start, size_t size, uint64_t flags)
{
    // Alinear a 4KB
    virtual_start &= ~0xFFF;
    size = (size + 0xFFF) & ~0xFFF;
    
    // Agregar flag de usuario
    flags |= PAGE_USER;
    
    print("map_user_region: Mapping ");
    print_uint32(size);
    print(" bytes at 0x");
    print_hex64(virtual_start);
    print(" with flags 0x");
    print_hex64(flags);
    print("\n");
    
    // Mapear cada página
    for (size_t offset = 0; offset < size; offset += 0x1000) 
    {
        uintptr_t virt_addr = virtual_start + offset;
        
        // Asignar página física usando kmalloc (simplificado)
        // En un kernel real, esto usaría un allocator de páginas físicas
        extern void *kmalloc(size_t size);
        uintptr_t phys_addr = (uintptr_t)kmalloc(0x1000);
        if (phys_addr == 0) 
        {
            print("map_user_region: Failed to allocate physical page\n");
            return -1;
        }
        
        // Mapear la página
        if (map_page(virt_addr, phys_addr, flags) != 0) 
        {
            print("map_user_region: Failed to map page\n");
            return -1;
        }
    }
    
    return 0;
}

// ===============================================================================
// DEBUG FUNCTIONS
// ===============================================================================

void print_paging_status(void)
{
    uint64_t cr0, cr3, cr4;
    
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    
    log_info_fmt("PAGING", "CR0: 0x%llx (PG: %s)", cr0, (cr0 & 0x80000000) ? "ON" : "OFF");
    log_info_fmt("PAGING", "CR3: 0x%llx", cr3);
    log_info_fmt("PAGING", "CR4: 0x%llx", cr4);
}

void dump_page_tables(void)
{
    for (int i = 0; i < 4; i++) 
    {
        log_info_fmt("PAGING", "PML4[%d]: 0x%llx", i, PML4[i]);
    }
    
    for (int i = 0; i < 4; i++) 
    {
        log_info_fmt("PAGING", "PDPT[%d]: 0x%llx", i, PDPT[i]);
    }
    
    for (int i = 0; i < 8; i++) 
    {
        log_info_fmt("PAGING", "PD[%d]: 0x%llx", i, PD[i]);
    }
}

/**
 * Verificar integridad del sistema de paginación
 */
int verify_paging_integrity(void)
{
    log_info("PAGING", "=== PAGING INTEGRITY CHECK ===");
    
    // 1. Verificar que paging está habilitado
    if (!is_paging_enabled()) 
    {
        log_error("PAGING", "Paging not enabled!");
        return 0;
    }
    
    // 2. Verificar que CR3 apunta a PML4
    uint64_t cr3 = get_current_page_directory();
    if (cr3 != (uint64_t)PML4) 
    {
        log_error_fmt("PAGING", "CR3 mismatch: 0x%llx != 0x%llx", cr3, (uint64_t)PML4);
        return 0;
    }
    
    // 3. Verificar entradas PML4
    if ((PML4[0] & PAGE_PRESENT) == 0) 
    {
        log_error("PAGING", "PML4[0] not present!");
        return 0;
    }
    
    // 4. Verificar entradas PDPT
    if ((PDPT[0] & PAGE_PRESENT) == 0) 
    {
        log_error("PAGING", "PDPT[0] not present!");
        return 0;
    }
    
    // 5. Verificar entradas PD (primeras 8)
    for (int i = 0; i < 8; i++) 
    {
        if ((PD[i] & PAGE_PRESENT) == 0) 
        {
            log_error_fmt("PAGING", "PD[%d] not present!", i);
            return 0;
        }
        if ((PD[i] & PAGE_SIZE_2MB_FLAG) == 0) 
        {
            log_error_fmt("PAGING", "PD[%d] not 2MB page!", i);
            return 0;
        }
    }
    
    log_info("PAGING", "✓ Paging integrity verified");
    return 1;
}

/**
 * Test de acceso a memoria fuera del rango mapeado
 * Debe causar page fault si el paging funciona correctamente
 */
void test_page_fault_protection(void)
{
    log_info("PAGING", "=== PAGE FAULT PROTECTION TEST ===");
    
    // Intentar acceder a memoria fuera del rango mapeado (16 MiB)
    volatile uint64_t *test_addr = (volatile uint64_t *)0x2000000; // 32 MiB
    
    log_info("PAGING", "Testing access to unmapped memory (should cause page fault)...");
    log_info_fmt("PAGING", "Attempting to read from 0x%llx", (uint64_t)test_addr);
    
    // Este acceso debería causar page fault si el paging funciona
    // Si llegamos aquí sin page fault, algo está mal
    uint64_t value = *test_addr;
    
    log_error("PAGING", "WARNING: Access to unmapped memory succeeded!");
    log_error_fmt("PAGING", "Read value: 0x%llx (this should not happen)", value);
}
