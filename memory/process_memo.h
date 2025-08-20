// memory/process_memory.h - Preparación para gestión de memoria por procesos
// NOTA: Esto es preparación futura, no se incluye en build actual

#pragma once
#include <stdint.h>
#include <print.h>
#include "memo_interface.h"

// ===============================================================================
// PROCESS MEMORY MANAGEMENT - Para cuando tengamos procesos reales
// ===============================================================================

typedef struct process_memory_space
{
   uintptr_t page_directory; // CR3 value para este proceso

   // Virtual memory areas del proceso
   struct
   {
      uintptr_t code_start, code_end;   // .text segment
      uintptr_t data_start, data_end;   // .data/.bss segment
      uintptr_t heap_start, heap_end;   // Process heap
      uintptr_t stack_start, stack_end; // Process stack
      uintptr_t mmap_start, mmap_end;   // mmap() area
   } layout;

   // Estadísticas
   size_t resident_pages; // Páginas físicas realmente allocadas
   size_t virtual_pages;  // Páginas virtuales reservadas
   size_t peak_memory;    // Máximo uso de memoria

   // Flags de estado
   uint32_t flags;
#define PROC_MEM_COPY_ON_WRITE (1 << 0)
#define PROC_MEM_LAZY_ALLOC (1 << 1)
#define PROC_MEM_SWAPPABLE (1 << 2)

} process_memory_space_t;

// ===============================================================================
// FUNCIONES IMPLEMENTADAS - MEMORY ISOLATION
// ===============================================================================

// Crear page directory para proceso
uintptr_t create_process_page_directory(void);

// Destruir page directory
void destroy_process_page_directory(uintptr_t pml4_phys);

// Cambiar page directory (context switch)
void switch_process_page_directory(uintptr_t pml4_phys);

// Mapear región en user space
int map_user_region(uintptr_t pml4_phys, uintptr_t virt_addr, size_t size, uint32_t flags);

// Unmapear región de user space
int unmap_user_region(uintptr_t pml4_phys, uintptr_t virt_addr, size_t size);

// Copy-on-write
int mark_page_cow(uintptr_t virt_addr);
int handle_cow_fault(uintptr_t fault_addr);

// Debug
void debug_process_memory(uintptr_t pml4_phys);
void debug_all_process_memory(void);

// ===============================================================================
// FUNCIONES FUTURAS (para ELF loader)
// ===============================================================================

// Crear espacio de memoria para nuevo proceso
process_memory_space_t *create_process_memory(void);

// Destruir espacio de memoria cuando proceso termina
void destroy_process_memory(process_memory_space_t *proc_mem);

// Cambiar contexto de memoria (cambiar CR3)
void switch_process_memory(process_memory_space_t *proc_mem);

// Clonar espacio de memoria (para fork())
process_memory_space_t *clone_process_memory(process_memory_space_t *parent);

// Mapear archivo en memoria del proceso
int process_mmap(process_memory_space_t *proc_mem, uintptr_t virt_addr,
                 size_t size, uint32_t flags);

// Gestión de heap por proceso
void *process_malloc(process_memory_space_t *proc_mem, size_t size);
void process_free(process_memory_space_t *proc_mem, void *ptr);

// ===============================================================================
// CONSIDERACIONES DE DISEÑO
// ===============================================================================

/*
PROBLEMA ACTUAL: El kernel actual es monolítico

Todo el código del kernel comparte el mismo espacio de direcciones:
- Una sola tabla de páginas global
- Una sola heap (kmalloc)
- Sin aislamiento entre "procesos" (solo tasks del scheduler)

PARA PROCESOS REALES NECESITAS:

1. Page Directory por proceso
   - Cada proceso tiene su propio CR3
   - Context switch incluye cambio de memoria virtual
   - Kernel space se mapea en todos los procesos (0x00000000-0x40000000)
   - User space es único por proceso (0x40000000-0x80000000)

2. Memory Management Unit por proceso
   - Fault handler que sepa qué proceso causó el fault
   - Page fault específico por proceso
   - Copy-on-write para fork()

3. Sistema unificado kernel/user
   - Syscalls para malloc/free de user space
   - Protección: user no puede acceder a kernel memory
   - Validación de punteros user en syscalls

CUÁNDO IMPLEMENTAR:
- Después de tener procesos básicos funcionando
- Después de tener syscalls básicos
- Después de tener ELF loader

ESTRATEGIA GRADUAL:
1. Terminar kernel memory management (actual)
2. Implementar procesos básicos con shared memory
3. Agregar memory isolation paso a paso
4. Finalmente, full process memory management
*/

// ===============================================================================
// COMPATIBILIDAD CON ARQUITECTURA ACTUAL
// ===============================================================================

// Para mantener compatibilidad, estas funciones wrappean el sistema actual
static inline void *kernel_malloc(size_t size) { return kmalloc(size); }
static inline void kernel_free(void *ptr) { kfree(ptr); }
static inline void *kernel_vmalloc(size_t size) { return vmalloc(size); }
static inline void kernel_vfree(void *ptr) { vfree(ptr); }

// Helper para debug
void debug_current_memory_layout(void);