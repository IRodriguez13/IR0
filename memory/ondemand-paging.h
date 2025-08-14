// memory/ondemand_paging.h - Header para paginación on-demand
#pragma once

#include <stdint.h>
#include <stddef.h>

// Forward declarations
typedef struct vm_area vm_area_t;

// ===============================================================================
// INICIALIZACIÓN Y CONFIGURACIÓN
// ===============================================================================

/**
 * Inicializar sistema de paginación on-demand
 * Debe llamarse después de memory_init()
 */
void ondemand_paging_init(void);

// ===============================================================================
// MANEJO DE ÁREAS DE MEMORIA VIRTUAL
// ===============================================================================

/**
 * Registrar un área de memoria virtual que soportará on-demand paging
 * @param start: dirección inicial (debe estar alineada a página)
 * @param end: dirección final (debe estar alineada a página)
 * @param flags: permisos (PAGE_FLAG_*)
 * @return: 0 on success, negative on error
 */
int vm_area_register(uintptr_t start, uintptr_t end, uint32_t flags);

/**
 * Encontrar área VM que contiene una dirección
 * @param virt_addr: dirección virtual a buscar
 * @return: puntero al área o NULL si no se encuentra
 */
vm_area_t *find_vm_area(uintptr_t virt_addr);

// ===============================================================================
// PAGE FAULT HANDLING
// ===============================================================================

/**
 * Manejar page fault con allocación on-demand
 * @param fault_addr: dirección que causó el fault
 * @param error_code: código de error x86 del page fault
 * @return: 0 si se resolvió, negativo si error
 */
int handle_page_fault_ondemand(uintptr_t fault_addr, uint32_t error_code);

/**
 * Validar permisos de page fault
 * @param vm_area: área VM donde ocurrió el fault
 * @param error_code: código de error x86
 * @return: 1 si permisos válidos, 0 si violación
 */
int validate_page_fault_permissions(vm_area_t *vm_area, uint32_t error_code);

/**
 * Page fault handler mejorado (reemplaza el actual en isr_handlers.c)
 * Esta función debe ser llamada desde el ASM interrupt handler
 */
void page_fault_handler_improved(void);

// ===============================================================================
// API DE ALTO NIVEL
// ===============================================================================

/**
 * Virtual malloc - reserva espacio virtual sin allocar páginas físicas
 * Las páginas se allocan cuando se acceden (lazy allocation)
 * @param size: tamaño a reservar (se redondea a páginas)
 * @return: puntero virtual o NULL si error
 */
void *vmalloc(size_t size);

/**
 * Liberar memoria virtual y páginas físicas asociadas
 * @param ptr: puntero retornado por vmalloc
 */
void vfree(void *ptr);

// ===============================================================================
// DEBUGGING Y ESTADÍSTICAS
// ===============================================================================

/**
 * Mostrar estado del sistema on-demand paging
 */
void debug_ondemand_paging(void);

/**
 * Obtener estadísticas de page faults
 */
typedef struct
{
    uint32_t total_page_faults;
    uint32_t pages_allocated_on_demand;
    uint32_t permission_violations;
    uint32_t out_of_memory_faults;
} ondemand_stats_t;

ondemand_stats_t get_ondemand_stats(void);

// ===============================================================================
// TESTING
// ===============================================================================

/**
 * Test básico de funcionalidad on-demand
 * Alloca memoria virtual y escribe en diferentes páginas
 */
void test_ondemand_paging(void);