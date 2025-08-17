// memory/memory_interface.h - Interfaz común para manejo de memoria
#pragma once

#include <stdint.h>
#include <stddef.h>

// ===============================================================================
// INTERFAZ COMÚN - Funciones que funcionan igual en todas las arquitecturas
// ===============================================================================

/*
    Funciones de memo virtual
*/
void virtual_allocator_init(void);

void *valloc(size_t size);
void vfree(void *ptr);

/**
 * Inicializa el sistema de memoria
 * Debe llamarse después de init_paging()
 */
void memory_init(void);

/**
 * Allocar memoria en kernel heap
 */
void *kmalloc(size_t size);

/**
 * Liberar memoria del kernel heap
 */
void kfree(void *ptr);

/**
 * Redimensionar bloque de memoria
 */
void *krealloc(void *ptr, size_t new_size);

/**
 * Allocar página física (4KB)
 * Retorna dirección física
 */
uintptr_t alloc_physical_page(void);

/**
 * Liberar página física
 */
void free_physical_page(uintptr_t phys_addr);

/**
 * Debug: mostrar estado de memoria
 */
void debug_memory_state(void);

// Variables globales para estadísticas
extern uint32_t free_pages_count;
extern uint32_t total_pages_count;

// ===============================================================================
// INTERFAZ ESPECÍFICA POR ARQUITECTURA - Se implementa en arch/*/
// ===============================================================================

/**
 * Mapear página virtual a física
 * arch-specific porque cada arquitectura tiene diferentes niveles de paginación
 */
int arch_map_page(uintptr_t virt_addr, uintptr_t phys_addr, uint32_t flags);

/**
 * Unmapear página virtual
 */
int arch_unmap_page(uintptr_t virt_addr);

/**
 * Obtener dirección física desde virtual
 */
uintptr_t arch_virt_to_phys(uintptr_t virt_addr);

/**
 * Invalidar TLB para página específica
 */
void arch_invalidate_page(uintptr_t virt_addr);

/**
 * Crear nuevo directorio de páginas para proceso
 */
uintptr_t arch_create_page_directory(void);

/**
 * Destruir directorio de páginas
 */
void arch_destroy_page_directory(uintptr_t page_dir);

/**
 * Cambiar contexto de memoria (cargar CR3/TTBR)
 */
void arch_switch_page_directory(uintptr_t page_dir);

// ===============================================================================
// FLAGS COMUNES (se traducen a específicos en cada arch)
// ===============================================================================

#define PAGE_FLAG_PRESENT (1 << 0)
#define PAGE_FLAG_WRITABLE (1 << 1)
#define PAGE_FLAG_USER (1 << 2)
#define PAGE_FLAG_EXECUTABLE (1 << 3) // Para NX bit en x64, XN en ARM

// ===============================================================================
// MACROS ÚTILES
// ===============================================================================

#define PAGE_SIZE 4096
#define PAGE_ALIGN(addr) (((addr) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define PAGE_ALIGN_DOWN(addr) ((addr) & ~(PAGE_SIZE - 1))
#define IS_PAGE_ALIGNED(addr) (((addr) & (PAGE_SIZE - 1)) == 0)
#define PAGE_FLAG_LAZY (1 << 4)      // Página con lazy allocation
#define PAGE_FLAG_COW (1 << 5)       // Copy-on-write (para futuro)
#define PAGE_FLAG_SWAPPABLE (1 << 6) // Puede ir a swap (para futuro)