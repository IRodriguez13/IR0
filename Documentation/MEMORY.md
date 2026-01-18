# IR0 Kernel Memory Management Subsystem

## Overview

The memory management subsystem provides three layers of memory allocation and management: Physical Memory Manager (PMM) for frame allocation, heap allocator for dynamic kernel memory, and paging system for virtual memory and process isolation.

## Architecture

The memory management subsystem consists of:

1. **Physical Memory Manager (PMM)** - Bitmap-based 4KB frame allocator
2. **Heap Allocator** - Free-list based dynamic memory allocator
3. **Paging System** - Virtual memory and process isolation

## Physical Memory Manager (PMM)

### Overview

The PMM manages physical memory frames (4KB pages) using a bitmap-based allocator. It tracks which physical frames are allocated and which are free.

### Location

- Header: `mm/pmm.h`
- Implementation: `mm/pmm.c`

### Initialization

```c
void pmm_init(uintptr_t mem_start, size_t mem_size);
```

**Boot Configuration:**
- Memory region: 8MB to 32MB (24MB total, ~6000 frames)
- Bitmap allocated from kernel heap
- All frames initially marked as free

**Parameters:**
- `mem_start` - Start of physical memory region (aligned to 4KB)
- `mem_size` - Size of memory region in bytes

### Operations

#### Frame Allocation

```c
uintptr_t pmm_alloc_frame(void);
```

**Algorithm:** First-fit bitmap search
- Scans bitmap for first free frame (bit = 0)
- Marks frame as used (bit = 1)
- Returns physical address of allocated frame
- Returns 0 on failure (out of memory)

**Time Complexity:** O(n) where n is number of frames

#### Frame Deallocation

```c
void pmm_free_frame(uintptr_t phys_addr);
```

**Behavior:**
- Validates address is within managed region
- Aligns address to 4KB boundary
- Calculates frame index from address
- Marks frame as free (bit = 0)
- Detects double-free attempts

### Statistics

```c
void pmm_stats(size_t *total_frames, size_t *used_frames, size_t *free_frames);
```

Provides statistics about PMM state:
- Total number of frames
- Number of allocated frames
- Number of free frames

### Bitmap Structure

- **Size:** 1 bit per 4KB frame
- **Storage:** Allocated from kernel heap
- **Encoding:** 1 = used, 0 = free
- **Access:** Bit operations for efficiency

## Heap Allocator

### Overview

The heap allocator provides dynamic memory allocation for kernel code using a free-list with boundary tags for efficient coalescing.

### Location

- Header: `includes/ir0/kmem.h`
- Implementation: `mm/allocator.c`

### Initialization

```c
void heap_init(void);
```

**Heap Setup:**
- Heap start/end addresses configured
- Initial free block covers entire heap
- Free list initialized with single block

### Allocation API

```c
void *kmalloc(size_t size);
void *kmalloc_aligned(size_t size, size_t alignment);
void *krealloc(void *ptr, size_t new_size);
void kfree(void *ptr);
void kfree_aligned(void *ptr);
```

**Macros:**
- `kmalloc(size)` - Allocate memory (wraps with debug info)
- `kfree(ptr)` - Free memory (wraps with debug info)
- Automatic caller location tracking via `__FILE__`, `__LINE__`, `__func__`

### Algorithm

**Free-List Structure:**
- Doubly-linked list of free blocks
- Each block has header (start) and footer (end)
- Header/footer contain size and free status

**Allocation (First-Fit):**
1. Search free list for block large enough
2. If found, split block if significantly larger than requested
3. Remove from free list (or update if split)
4. Mark as allocated
5. Return pointer to user data (after header)

**Deallocation (with Coalescing):**
1. Mark block as free
2. Check if next block is free → coalesce forward
3. Check if previous block is free → coalesce backward (using footer)
4. Add coalesced block to free list

**Coalescing:**
- **Forward:** O(1) - check next physical block
- **Backward:** O(1) - use footer to find previous block header
- Reduces fragmentation

### Block Structure

```c
struct block_header {
    size_t size;               /* Total block size (including header/footer) */
    int is_free;              /* 1 = free, 0 = allocated */
    struct block_header *next; /* Next in free list */
    struct block_header *prev; /* Previous in free list */
};

struct block_footer {
    size_t size;              /* Must match header->size */
    int is_free;             /* Mirror of header status */
};
```

**Layout:**
```
[Header] [User Data] [Footer]
```

### Alignment Support

`kmalloc_aligned()` provides alignment guarantees:
- Allocates block large enough to satisfy alignment
- Adjusts user pointer to aligned address
- Maintains original pointer for `kfree_aligned()`

## Paging System

### Overview

The paging system provides virtual memory management and process isolation through separate page directories.

### Location

- Header: `mm/paging.h`
- Implementation: `mm/paging.c`

### Page Sizes

- **4KB pages** - Standard page size (PAGE_SIZE_4KB)
- **2MB pages** - Large pages for identity mapping (PAGE_SIZE_2MB)
- **1GB pages** - Huge pages (defined but not used)

### Page Flags

```c
PAGE_PRESENT     0x1   /* Page is present in memory */
PAGE_RW          0x2   /* Read/write (vs read-only) */
PAGE_USER        0x4   /* User accessible (vs kernel only) */
PAGE_WRITETHROUGH 0x8  /* Write-through caching */
PAGE_CACHE_DISABLE 0x10 /* Disable caching */
PAGE_ACCESSED    0x20  /* Page has been accessed */
PAGE_DIRTY       0x40  /* Page has been written */
PAGE_SIZE_2MB_FLAG 0x80 /* 2MB page (vs 4KB) */
PAGE_GLOBAL      0x100 /* Global page (TLB) */
```

### Initialization

```c
void setup_and_enable_paging(void);
void setup_paging_identity_16mb(void);
void enable_paging(void);
```

**Boot Setup:**
1. Verifies 64-bit mode and PAE enabled
2. Creates identity mapping for first 16MB using 2MB pages
3. Enables paging (CR0.PG bit)
4. Kernel space identity mapped (virtual = physical)

### Page Directory

**x86-64 Structure:**
- **PML4 (Page Map Level 4)** - Top level, 512 entries
- **PDPT (Page Directory Pointer Table)** - Second level
- **PD (Page Directory)** - Third level
- **PT (Page Table)** - Bottom level for 4KB pages

**CR3 Register:**
- Points to PML4 table
- Loaded during context switch for process isolation
- Kernel directory shared, user directories process-specific

### Process Isolation

Each process has its own page directory:

```c
uint64_t create_process_page_directory(void);
```

**Process Memory Layout:**
- **Kernel Space** - Shared identity mapping (virtual = physical)
- **User Space** - Process-specific virtual addresses
- **Stack** - Private per-process stack
- **Heap** - Managed via `brk()` syscall

### Page Mapping

```c
int map_page(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);
int unmap_page(uint64_t virt_addr);
```

**Mapping Process:**
1. Traverse page directory hierarchy
2. Allocate page tables if needed
3. Set page table entry with physical address and flags
4. Invalidate TLB if necessary

**Current Implementation:**
- Basic mapping support
- Used primarily for process page directory creation
- Not fully utilized for dynamic memory mapping

### Page Directory Loading

```c
void load_page_directory(uint64_t pml4_addr);
uint64_t get_current_page_directory(void);
```

**Usage:**
- Load process page directory during context switch
- Kernel directory loaded at boot
- Process directories loaded when process scheduled

## Memory Regions

### Kernel Memory Map

```
0x000000 - 0x100000  Boot code, kernel code
0x100000 - 0x800000  Kernel heap (grows upward)
0x800000 - 0x2000000 PMM managed frames (8MB-32MB, 24MB)
0x2000000+          Kernel data, stacks
```

### Process Memory Layout

Each process has:
- **Code Segment** - ELF binary loaded into user space
- **Stack** - 8KB default, grows downward from high address
- **Heap** - Grows upward from low address, managed by `brk()`
- **Page Directory** - Isolated virtual address space

## Error Handling

All memory operations handle errors gracefully:

- **PMM:** Returns 0 on allocation failure (out of memory)
- **Heap:** Returns NULL on allocation failure
- **Paging:** Returns error codes for invalid operations

**Error Logging:**
- All errors logged to serial console
- Debug flags (`DEBUG_PMM`) for detailed logging
- Statistics available for debugging

## Memory Statistics

Available via `/proc/meminfo`:
- Total physical memory
- Used/free frames (PMM statistics)
- Heap allocator statistics
- Per-process memory usage

## Implementation Notes

- **No Copy-on-Write** - Fork operations don't use COW (intentional limitation)
- **No Swap** - All memory is physical (no disk swapping)
- **Simple Algorithms** - First-fit for both PMM and heap (not optimized)
- **No NUMA** - Assumes uniform memory access
- **Single CPU** - No SMP-aware memory management

## Integration Points

Memory management integrates with:

1. **Process Management** - Provides page directories for process isolation
2. **Scheduler** - Switches page directories (CR3) during context switch
3. **Drivers** - Uses heap allocator for driver data structures
4. **Filesystem** - Uses heap for file system buffers and structures

---

# Subsistema de Gestión de Memoria del Kernel IR0

## Resumen

El subsistema de gestión de memoria proporciona tres capas de asignación y gestión de memoria: Administrador de Memoria Física (PMM) para asignación de frames, asignador de heap para memoria dinámica del kernel, y sistema de paginación para memoria virtual y aislamiento de procesos.

## Arquitectura

El subsistema de gestión de memoria consta de:

1. **Administrador de Memoria Física (PMM)** - Asignador de frames de 4KB basado en bitmap
2. **Asignador de Heap** - Asignador de memoria dinámica basado en free-list
3. **Sistema de Paginación** - Memoria virtual y aislamiento de procesos

## Administrador de Memoria Física (PMM)

### Resumen

El PMM gestiona frames de memoria física (páginas de 4KB) usando un asignador basado en bitmap. Rastrea qué frames físicos están asignados y cuáles están libres.

### Ubicación

- Header: `includes/ir0/memory/pmm.h`
- Implementación: `includes/ir0/memory/pmm.c`

### Inicialización

```c
void pmm_init(uintptr_t mem_start, size_t mem_size);
```

**Configuración de Arranque:**
- Región de memoria: 8MB a 32MB (24MB total, ~6000 frames)
- Bitmap asignado desde heap del kernel
- Todos los frames inicialmente marcados como libres

**Parámetros:**
- `mem_start` - Inicio de la región de memoria física (alineado a 4KB)
- `mem_size` - Tamaño de la región de memoria en bytes

### Operaciones

#### Asignación de Frame

```c
uintptr_t pmm_alloc_frame(void);
```

**Algoritmo:** Búsqueda first-fit en bitmap
- Escanea bitmap buscando primer frame libre (bit = 0)
- Marca frame como usado (bit = 1)
- Retorna dirección física del frame asignado
- Retorna 0 en fallo (sin memoria)

**Complejidad Temporal:** O(n) donde n es el número de frames

#### Desasignación de Frame

```c
void pmm_free_frame(uintptr_t phys_addr);
```

**Comportamiento:**
- Valida que la dirección esté dentro de la región gestionada
- Alinea dirección a límite de 4KB
- Calcula índice de frame desde dirección
- Marca frame como libre (bit = 0)
- Detecta intentos de double-free

### Estadísticas

```c
void pmm_stats(size_t *total_frames, size_t *used_frames, size_t *free_frames);
```

Proporciona estadísticas sobre el estado del PMM:
- Número total de frames
- Número de frames asignados
- Número de frames libres

### Estructura del Bitmap

- **Tamaño:** 1 bit por frame de 4KB
- **Almacenamiento:** Asignado desde heap del kernel
- **Codificación:** 1 = usado, 0 = libre
- **Acceso:** Operaciones de bits para eficiencia

## Asignador de Heap

### Resumen

El asignador de heap proporciona asignación dinámica de memoria para código del kernel usando una free-list con boundary tags para coalescencia eficiente.

### Ubicación

- Header: `includes/ir0/memory/kmem.h`
- Implementación: `includes/ir0/memory/allocator.c`

### Inicialización

```c
void heap_init(void);
```

**Configuración del Heap:**
- Direcciones de inicio/fin del heap configuradas
- Bloque libre inicial cubre todo el heap
- Free list inicializada con un solo bloque

### API de Asignación

```c
void *kmalloc(size_t size);
void *kmalloc_aligned(size_t size, size_t alignment);
void *krealloc(void *ptr, size_t new_size);
void kfree(void *ptr);
void kfree_aligned(void *ptr);
```

**Macros:**
- `kmalloc(size)` - Asignar memoria (envuelve con información de debug)
- `kfree(ptr)` - Liberar memoria (envuelve con información de debug)
- Rastreo automático de ubicación del llamador via `__FILE__`, `__LINE__`, `__func__`

### Algoritmo

**Estructura de Free-List:**
- Lista doblemente enlazada de bloques libres
- Cada bloque tiene header (inicio) y footer (fin)
- Header/footer contienen tamaño y estado libre

**Asignación (First-Fit):**
1. Busca en free list un bloque suficientemente grande
2. Si se encuentra, divide bloque si es significativamente más grande que lo solicitado
3. Elimina de free list (o actualiza si se dividió)
4. Marca como asignado
5. Retorna puntero a datos de usuario (después del header)

**Desasignación (con Coalescencia):**
1. Marca bloque como libre
2. Verifica si el siguiente bloque es libre → coalesce hacia adelante
3. Verifica si el bloque anterior es libre → coalesce hacia atrás (usando footer)
4. Agrega bloque coalescido a free list

**Coalescencia:**
- **Hacia Adelante:** O(1) - verifica siguiente bloque físico
- **Hacia Atrás:** O(1) - usa footer para encontrar header del bloque anterior
- Reduce fragmentación

### Estructura de Bloque

```c
struct block_header {
    size_t size;               /* Tamaño total del bloque (incluyendo header/footer) */
    int is_free;              /* 1 = libre, 0 = asignado */
    struct block_header *next; /* Siguiente en free list */
    struct block_header *prev; /* Anterior en free list */
};

struct block_footer {
    size_t size;              /* Debe coincidir con header->size */
    int is_free;             /* Espejo del estado del header */
};
```

**Layout:**
```
[Header] [Datos de Usuario] [Footer]
```

### Soporte de Alineación

`kmalloc_aligned()` proporciona garantías de alineación:
- Asigna bloque suficientemente grande para satisfacer alineación
- Ajusta puntero de usuario a dirección alineada
- Mantiene puntero original para `kfree_aligned()`

## Sistema de Paginación

### Resumen

El sistema de paginación proporciona gestión de memoria virtual y aislamiento de procesos a través de directorios de página separados.

### Ubicación

- Header: `includes/ir0/memory/paging.h`
- Implementación: `includes/ir0/memory/paging.c`

### Tamaños de Página

- **Páginas de 4KB** - Tamaño de página estándar (PAGE_SIZE_4KB)
- **Páginas de 2MB** - Páginas grandes para mapeo idéntico (PAGE_SIZE_2MB)
- **Páginas de 1GB** - Páginas enormes (definidas pero no usadas)

### Banderas de Página

```c
PAGE_PRESENT     0x1   /* Página está presente en memoria */
PAGE_RW          0x2   /* Lectura/escritura (vs solo lectura) */
PAGE_USER        0x4   /* Accesible por usuario (vs solo kernel) */
PAGE_WRITETHROUGH 0x8  /* Caché write-through */
PAGE_CACHE_DISABLE 0x10 /* Deshabilitar caché */
PAGE_ACCESSED    0x20  /* Página ha sido accedida */
PAGE_DIRTY       0x40  /* Página ha sido escrita */
PAGE_SIZE_2MB_FLAG 0x80 /* Página de 2MB (vs 4KB) */
PAGE_GLOBAL      0x100 /* Página global (TLB) */
```

### Inicialización

```c
void setup_and_enable_paging(void);
void setup_paging_identity_16mb(void);
void enable_paging(void);
```

**Configuración de Arranque:**
1. Verifica modo 64-bit y PAE habilitado
2. Crea mapeo idéntico para primeros 16MB usando páginas de 2MB
3. Habilita paginación (bit CR0.PG)
4. Espacio del kernel mapeado idéntico (virtual = físico)

### Directorio de Página

**Estructura x86-64:**
- **PML4 (Page Map Level 4)** - Nivel superior, 512 entradas
- **PDPT (Page Directory Pointer Table)** - Segundo nivel
- **PD (Page Directory)** - Tercer nivel
- **PT (Page Table)** - Nivel inferior para páginas de 4KB

**Registro CR3:**
- Apunta a tabla PML4
- Se carga durante cambio de contexto para aislamiento de proceso
- Directorio del kernel compartido, directorios de usuario específicos de proceso

### Aislamiento de Proceso

Cada proceso tiene su propio directorio de página:

```c
uint64_t create_process_page_directory(void);
```

**Layout de Memoria del Proceso:**
- **Espacio del Kernel** - Mapeo idéntico compartido (virtual = físico)
- **Espacio de Usuario** - Direcciones virtuales específicas del proceso
- **Stack** - Stack privado por proceso
- **Heap** - Gestionado via syscall `brk()`

### Mapeo de Páginas

```c
int map_page(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);
int unmap_page(uint64_t virt_addr);
```

**Proceso de Mapeo:**
1. Atraviesa jerarquía de directorio de página
2. Asigna tablas de página si es necesario
3. Establece entrada de tabla de página con dirección física y banderas
4. Invalida TLB si es necesario

**Implementación Actual:**
- Soporte básico de mapeo
- Usado principalmente para creación de directorio de página de proceso
- No completamente utilizado para mapeo dinámico de memoria

### Carga de Directorio de Página

```c
void load_page_directory(uint64_t pml4_addr);
uint64_t get_current_page_directory(void);
```

**Uso:**
- Carga directorio de página de proceso durante cambio de contexto
- Directorio del kernel cargado al arrancar
- Directorios de proceso cargados cuando se planifica el proceso

## Regiones de Memoria

### Mapa de Memoria del Kernel

```
0x000000 - 0x100000  Código de arranque, código del kernel
0x100000 - 0x800000  Heap del kernel (crece hacia arriba)
0x800000 - 0x2000000 Frames gestionados por PMM (8MB-32MB, 24MB)
0x2000000+          Datos del kernel, stacks
```

### Layout de Memoria del Proceso

Cada proceso tiene:
- **Segmento de Código** - Binario ELF cargado en espacio de usuario
- **Stack** - 8KB por defecto, crece hacia abajo desde dirección alta
- **Heap** - Crece hacia arriba desde dirección baja, gestionado por `brk()`
- **Directorio de Página** - Espacio de direcciones virtuales aislado

## Manejo de Errores

Todas las operaciones de memoria manejan errores graciosamente:

- **PMM:** Retorna 0 en fallo de asignación (sin memoria)
- **Heap:** Retorna NULL en fallo de asignación
- **Paginación:** Retorna códigos de error para operaciones inválidas

**Registro de Errores:**
- Todos los errores registrados en consola serial
- Banderas de debug (`DEBUG_PMM`) para registro detallado
- Estadísticas disponibles para debugging

## Estadísticas de Memoria

Disponibles via `/proc/meminfo`:
- Memoria física total
- Frames usados/libres (estadísticas PMM)
- Estadísticas del asignador de heap
- Uso de memoria por proceso

## Notas de Implementación

- **Sin Copy-on-Write** - Operaciones fork no usan COW (limitación intencional)
- **Sin Swap** - Toda la memoria es física (sin intercambio a disco)
- **Algoritmos Simples** - First-fit tanto para PMM como heap (no optimizado)
- **Sin NUMA** - Asume acceso uniforme a memoria
- **CPU Única** - Sin gestión de memoria consciente de SMP

## Puntos de Integración

La gestión de memoria se integra con:

1. **Gestión de Procesos** - Proporciona directorios de página para aislamiento de procesos
2. **Planificador** - Cambia directorios de página (CR3) durante cambio de contexto
3. **Drivers** - Usa asignador de heap para estructuras de datos de drivers
4. **Sistema de Archivos** - Usa heap para buffers y estructuras del sistema de archivos

