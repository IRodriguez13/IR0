// memory/process_memory.c - Implementación de aislamiento de memoria por procesos
// Usando la infraestructura de memoria existente del kernel IR0

#include "process_memo.h"
#include "../../includes/ir0/print.h"
#include "memo_interface.h"
#include "physical_allocator.h"
#include "../arch/common/arch_interface.h"
#include <print.h>
#include <panic/panic.h>
#include <string.h>

// ===============================================================================
// ESTRUCTURAS PARA MEMORIA DE PROCESOS
// ===============================================================================

typedef struct process_page_directory
{
    uintptr_t pml4_phys;           // Dirección física del PML4
    uint64_t *pml4_virt;           // Dirección virtual del PML4
    uint32_t ref_count;            // Contador de referencias
    struct process_page_directory *next;
} process_page_directory_t;

// Lista global de page directories
static process_page_directory_t *page_directories = NULL;

// ===============================================================================
// FUNCIONES DE GESTIÓN DE PAGE DIRECTORIES
// ===============================================================================

// Crear nuevo page directory para un proceso
uintptr_t create_process_page_directory(void)
{
    // Allocar página física para el PML4
    uintptr_t pml4_phys = alloc_physical_page();
    if (!pml4_phys) {
        LOG_ERR("create_process_page_directory: No physical memory for PML4");
        return 0;
    }
    
    // Mapear PML4 en memoria virtual del kernel
    uint64_t *pml4_virt = (uint64_t *)valloc(PAGE_SIZE);
    if (!pml4_virt) {
        free_physical_page(pml4_phys);
        LOG_ERR("create_process_page_directory: No virtual memory for PML4");
        return 0;
    }
    
    // Mapear la página física a la dirección virtual
    int result = arch_map_page((uintptr_t)pml4_virt, pml4_phys, 
                              PAGE_FLAG_PRESENT | PAGE_FLAG_WRITABLE);
    if (result != 0) {
        vfree(pml4_virt);
        free_physical_page(pml4_phys);
        LOG_ERR("create_process_page_directory: Failed to map PML4");
        return 0;
    }
    
    // Limpiar PML4 (todas las entradas en 0)
    memset(pml4_virt, 0, PAGE_SIZE);
    
    // Copiar mapeo del kernel desde el PML4 global
    // Esto asegura que todos los procesos tengan acceso al kernel space
    extern uint64_t PML4[512]; // PML4 global del kernel
    
    // Copiar entradas del kernel (primeras 256 entradas = kernel space)
    for (int i = 0; i < 256; i++) {
        pml4_virt[i] = PML4[i];
    }
    
    // Crear estructura de tracking
    process_page_directory_t *pdir = kmalloc(sizeof(process_page_directory_t));
    if (!pdir) {
        arch_unmap_page((uintptr_t)pml4_virt);
        vfree(pml4_virt);
        free_physical_page(pml4_phys);
        LOG_ERR("create_process_page_directory: No memory for tracking structure");
        return 0;
    }
    
    pdir->pml4_phys = pml4_phys;
    pdir->pml4_virt = pml4_virt;
    pdir->ref_count = 1;
    pdir->next = page_directories;
    page_directories = pdir;
    
    print("Created process page directory at 0x");
    print_hex(pml4_phys);
    print("\n");
    return pml4_phys;
}

// Destruir page directory
void destroy_process_page_directory(uintptr_t pml4_phys)
{
    process_page_directory_t *pdir = page_directories;
    process_page_directory_t *prev = NULL;
    
    while (pdir) {
        if (pdir->pml4_phys == pml4_phys) {
            pdir->ref_count--;
            
            if (pdir->ref_count == 0) {
                // Remover de lista
                if (prev) {
                    prev->next = pdir->next;
                } else {
                    page_directories = pdir->next;
                }
                
                // Unmapear y liberar
                arch_unmap_page((uintptr_t)pdir->pml4_virt);
                vfree(pdir->pml4_virt);
                free_physical_page(pdir->pml4_phys);
                kfree(pdir);
                
                print("Destroyed process page directory at 0x");
                print_hex(pml4_phys);
                print("\n");
            }
            return;
        }
        prev = pdir;
        pdir = pdir->next;
    }
    
    LOG_WARN("destroy_process_page_directory: Page directory not found");
}

// Cambiar page directory (context switch)
void switch_process_page_directory(uintptr_t pml4_phys)
{
    if (pml4_phys == 0) {
        LOG_ERR("switch_process_page_directory: Invalid PML4 address");
        return;
    }
    
    // Cambiar CR3 para el nuevo proceso
    arch_switch_page_directory(pml4_phys);
    
    print("Switched to process page directory at 0x");
    print_hex(pml4_phys);
    print("\n");
}

// ===============================================================================
// FUNCIONES PARA USER SPACE
// ===============================================================================

// Mapear región en user space
int map_user_region(uintptr_t pml4_phys, uintptr_t virt_addr, size_t size, uint32_t flags)
{
    if (!IS_PAGE_ALIGNED(virt_addr) || !IS_PAGE_ALIGNED(size)) {
        LOG_ERR("map_user_region: Address or size not page aligned");
        return -1;
    }
    
    // Verificar que está en user space
    if (virt_addr < USER_SPACE_BASE || virt_addr >= USER_SPACE_END) {
        LOG_ERR("map_user_region: Address not in user space");
        return -1;
    }
    
    // Agregar flag de usuario
    flags |= PAGE_FLAG_USER;
    
    // Mapear páginas una por una
    for (uintptr_t addr = virt_addr; addr < virt_addr + size; addr += PAGE_SIZE) {
        uintptr_t phys_page = alloc_physical_page();
        if (!phys_page) {
            LOG_ERR("map_user_region: Out of physical memory");
            return -1;
        }
        
        // Limpiar página
        memset((void *)phys_page, 0, PAGE_SIZE);
        
        // Mapear en el page directory del proceso
        // TODO: Implementar mapeo específico por proceso
        // Por ahora, usar el mapeo global
        int result = arch_map_page(addr, phys_page, flags);
        if (result != 0) {
            free_physical_page(phys_page);
            print("map_user_region: Failed to map page at 0x");
            print_hex(addr);
            print("\n");
            return -1;
        }
    }
    
    print("Mapped user region: 0x");
    print_hex(virt_addr);
    print(" - 0x");
    print_hex(virt_addr + size);
    print("\n");
    return 0;
}

// Unmapear región de user space
int unmap_user_region(uintptr_t pml4_phys, uintptr_t virt_addr, size_t size)
{
    if (!IS_PAGE_ALIGNED(virt_addr) || !IS_PAGE_ALIGNED(size)) {
        LOG_ERR("unmap_user_region: Address or size not page aligned");
        return -1;
    }
    
    // Unmapear páginas una por una
    for (uintptr_t addr = virt_addr; addr < virt_addr + size; addr += PAGE_SIZE) {
        // Obtener dirección física
        uintptr_t phys_page = arch_virt_to_phys(addr);
        if (phys_page != 0) {
            // Unmapear página
            arch_unmap_page(addr);
            // Liberar página física
            free_physical_page(phys_page);
        }
    }
    
    print("Unmapped user region: 0x");
    print_hex(virt_addr);
    print(" - 0x");
    print_hex(virt_addr + size);
    print("\n");
    return 0;
}

// ===============================================================================
// FUNCIONES PARA COPY-ON-WRITE
// ===============================================================================

// Marcar página como copy-on-write
int mark_page_cow(uintptr_t virt_addr)
{
    // TODO: Implementar copy-on-write
    // Por ahora, solo log
    print("Marked page as copy-on-write: 0x");
    print_hex(virt_addr);
    print("\n");
    return 0;
}

// Manejar page fault de copy-on-write
int handle_cow_fault(uintptr_t fault_addr)
{
    // TODO: Implementar manejo de COW fault
    // 1. Allocar nueva página
    // 2. Copiar contenido
    // 3. Mapear nueva página
    // 4. Marcar como writable
    
    print("Handling copy-on-write fault at: 0x");
    print_hex(fault_addr);
    print("\n");
    return 0;
}

// ===============================================================================
// FUNCIONES DE DEBUG
// ===============================================================================

void debug_process_memory(uintptr_t pml4_phys)
{
    print("=== Process Memory Debug ===\n");
    print("PML4 Physical Address: 0x");
    print_hex(pml4_phys);
    print("\n");
    
    // Contar páginas mapeadas
    int mapped_pages = 0;
    // TODO: Implementar conteo de páginas mapeadas
    
    print("Mapped Pages: ");
    print_int32(mapped_pages);
    print("\n");
    print("===========================\n");
}

// Helper para debug
void debug_current_memory_layout(void)
{
    print("=== CURRENT KERNEL MEMORY LAYOUT ===\n");

    print("KERNEL AREAS:\n");
    print("  Code/Data:    0x00000000 - 0x04000000 (64MB)\n");
    print("  Kernel Heap:  0x04000000 - 0x06000000 (32MB)\n");
    print("  Kernel Stack: 0x06000000 - 0x06400000 (4MB)\n");
    print("  VMalloc:      0x10000000 - 0x20000000 (256MB)\n");
    print("\n");

    print("FUTURE PROCESS AREAS:\n");
    print("  User Code:    0x40000000 - 0x60000000 (512MB)\n");
    print("  User Heap:    0x60000000 - 0x70000000 (256MB)\n");
    print("  User Stack:   0x70000000 - 0x80000000 (256MB)\n");
    print("\n");

    print("STATUS: Single address space (kernel only)\n");
    print("NEXT STEP: Implement basic processes with shared memory\n");
    print("FUTURE: Per-process page directories\n");
    print("\n");
}

void debug_all_process_memory(void)
{
    print("=== All Process Memory ===\n");
    
    process_page_directory_t *pdir = page_directories;
    int count = 0;
    
    while (pdir) {
        print("Process ");
        print_int32(count++);
        print(": PML4=0x");
        print_hex(pdir->pml4_phys);
        print(", refs=");
        print_int32(pdir->ref_count);
        print("\n");
        pdir = pdir->next;
    }
    
    print("Total Process Page Directories: ");
    print_int32(count);
    print("\n");
    print("===========================\n");
}
