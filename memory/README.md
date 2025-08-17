# Memory Management Subsystem

## English

### Overview
The Memory Management Subsystem is a comprehensive memory management system that provides physical memory allocation, virtual memory management, heap allocation, and process memory isolation. It supports both x86-32 and x86-64 architectures with advanced features like on-demand paging and memory verification.

### Key Components

#### 1. Physical Memory Allocator (`physical_allocator.c/h`)
- **Purpose**: Manages physical memory pages using a bitmap-based allocation system
- **Features**:
  - Bitmap-based tracking for efficient allocation/deallocation
  - Support for different page sizes (4KB, 2MB, 1GB)
  - Memory zone management (DMA, Normal, High)
  - Automatic memory coalescing
  - Statistics and debugging information

#### 2. Heap Allocator (`heap_allocator.c/h`)
- **Purpose**: Provides dynamic memory allocation for the kernel (kmalloc/kfree)
- **Features**:
  - Multiple allocation pools for different sizes
  - Fragmentation management with coalescing
  - Memory leak detection
  - Performance optimization for common allocation sizes
  - Debug information and statistics

#### 3. Virtual Memory Allocator (`vallocator.c`)
- **Purpose**: Manages virtual memory spaces and process memory isolation
- **Features**:
  - Virtual address space allocation
  - Process memory isolation
  - Memory protection (read, write, execute)
  - Shared memory support
  - Memory mapping and unmapping

#### 4. On-Demand Paging (`ondemand-paging.c/h`)
- **Purpose**: Implements demand paging for efficient memory usage
- **Features**:
  - Lazy page allocation
  - Page fault handling
  - Memory pressure management
  - Page replacement algorithms
  - Memory compression support

#### 5. Process Memory Management (`process_memo.h`)
- **Purpose**: Defines structures and interfaces for process memory management
- **Features**:
  - Process memory space definitions
  - Memory layout specifications
  - Process memory isolation
  - Memory sharing between processes

#### 6. Kernel Memory Layout (`krnl_memo_layout.h`)
- **Purpose**: Defines the kernel's memory layout and addressing scheme
- **Features**:
  - Kernel space definitions
  - Memory mapping specifications
  - Architecture-specific layouts
  - Memory protection zones

### Architecture Support

#### x86-32 Support (`arch/x_86-32/`)
- 4KB page tables
- 4GB address space
- 32-bit physical addressing
- PAE support for >4GB RAM

#### x86-64 Support (`arch/x86-64/`)
- 4KB, 2MB, and 1GB pages
- 256TB virtual address space
- 64-bit physical addressing
- Advanced paging features

### Key Features

1. **Multi-Architecture Support**
   - Native support for x86-32 and x86-64
   - Architecture-specific optimizations
   - Conditional compilation for different platforms

2. **Advanced Memory Management**
   - 1GB identity mapping for kernel stability
   - Memory verification system
   - Automatic memory mapping validation
   - Robust error handling and recovery

3. **Performance Optimizations**
   - Efficient bitmap-based allocation
   - Multiple allocation pools
   - Memory coalescing
   - Cache-friendly data structures

4. **Debugging and Monitoring**
   - Memory usage statistics
   - Leak detection
   - Performance profiling
   - Debug information output

### Usage Examples

```c
// Physical memory allocation
uintptr_t phys_addr = allocate_physical_page();
free_physical_page(phys_addr);

// Heap allocation
void* ptr = kmalloc(1024);
kfree(ptr);

// Virtual memory allocation
void* virt_addr = valloc(4096, VM_READ | VM_WRITE);
vfree(virt_addr);

// Memory mapping
map_page(virt_addr, phys_addr, VM_READ | VM_WRITE);
unmap_page(virt_addr);
```

### Configuration

The memory subsystem can be configured through the strategy system:
- **Desktop**: 256MB heap, full features
- **Server**: 1GB heap, optimized for high throughput
- **IoT**: 16MB heap, power-efficient
- **Embedded**: 4MB heap, minimal footprint

---

## Español

### Descripción General
El Subsistema de Gestión de Memoria es un sistema completo de gestión de memoria que proporciona asignación de memoria física, gestión de memoria virtual, asignación de heap y aislamiento de memoria por proceso. Soporta arquitecturas x86-32 y x86-64 con características avanzadas como paginación bajo demanda y verificación de memoria.

### Componentes Principales

#### 1. Asignador de Memoria Física (`physical_allocator.c/h`)
- **Propósito**: Gestiona páginas de memoria física usando un sistema de asignación basado en bitmap
- **Características**:
  - Seguimiento basado en bitmap para asignación/liberación eficiente
  - Soporte para diferentes tamaños de página (4KB, 2MB, 1GB)
  - Gestión de zonas de memoria (DMA, Normal, High)
  - Coalescencia automática de memoria
  - Estadísticas e información de debug

#### 2. Asignador de Heap (`heap_allocator.c/h`)
- **Propósito**: Proporciona asignación dinámica de memoria para el kernel (kmalloc/kfree)
- **Características**:
  - Múltiples pools de asignación para diferentes tamaños
  - Gestión de fragmentación con coalescencia
  - Detección de memory leaks
  - Optimización de rendimiento para tamaños comunes
  - Información de debug y estadísticas

#### 3. Asignador de Memoria Virtual (`vallocator.c`)
- **Propósito**: Gestiona espacios de memoria virtual y aislamiento de memoria por proceso
- **Características**:
  - Asignación de espacios de direcciones virtuales
  - Aislamiento de memoria por proceso
  - Protección de memoria (lectura, escritura, ejecución)
  - Soporte para memoria compartida
  - Mapeo y desmapeo de memoria

#### 4. Paginación Bajo Demanda (`ondemand-paging.c/h`)
- **Propósito**: Implementa paginación bajo demanda para uso eficiente de memoria
- **Características**:
  - Asignación lazy de páginas
  - Manejo de page faults
  - Gestión de presión de memoria
  - Algoritmos de reemplazo de páginas
  - Soporte para compresión de memoria

#### 5. Gestión de Memoria de Procesos (`process_memo.h`)
- **Propósito**: Define estructuras e interfaces para gestión de memoria de procesos
- **Características**:
  - Definiciones de espacios de memoria de procesos
  - Especificaciones de layout de memoria
  - Aislamiento de memoria por proceso
  - Compartición de memoria entre procesos

#### 6. Layout de Memoria del Kernel (`krnl_memo_layout.h`)
- **Propósito**: Define el layout de memoria del kernel y esquema de direccionamiento
- **Características**:
  - Definiciones de espacio del kernel
  - Especificaciones de mapeo de memoria
  - Layouts específicos por arquitectura
  - Zonas de protección de memoria

### Soporte de Arquitecturas

#### Soporte x86-32 (`arch/x_86-32/`)
- Tablas de páginas de 4KB
- Espacio de direcciones de 4GB
- Direccionamiento físico de 32 bits
- Soporte PAE para >4GB RAM

#### Soporte x86-64 (`arch/x86-64/`)
- Páginas de 4KB, 2MB y 1GB
- Espacio de direcciones virtual de 256TB
- Direccionamiento físico de 64 bits
- Características avanzadas de paginación

### Características Principales

1. **Soporte Multi-Arquitectura**
   - Soporte nativo para x86-32 y x86-64
   - Optimizaciones específicas por arquitectura
   - Compilación condicional para diferentes plataformas

2. **Gestión Avanzada de Memoria**
   - Mapeo de identidad de 1GB para estabilidad del kernel
   - Sistema de verificación de memoria
   - Validación automática de mapeo de memoria
   - Manejo robusto de errores y recuperación

3. **Optimizaciones de Rendimiento**
   - Asignación eficiente basada en bitmap
   - Múltiples pools de asignación
   - Coalescencia de memoria
   - Estructuras de datos cache-friendly

4. **Debugging y Monitoreo**
   - Estadísticas de uso de memoria
   - Detección de leaks
   - Profiling de rendimiento
   - Salida de información de debug

### Ejemplos de Uso

```c
// Asignación de memoria física
uintptr_t phys_addr = allocate_physical_page();
free_physical_page(phys_addr);

// Asignación de heap
void* ptr = kmalloc(1024);
kfree(ptr);

// Asignación de memoria virtual
void* virt_addr = valloc(4096, VM_READ | VM_WRITE);
vfree(virt_addr);

// Mapeo de memoria
map_page(virt_addr, phys_addr, VM_READ | VM_WRITE);
unmap_page(virt_addr);
```

### Configuración

El subsistema de memoria se puede configurar a través del sistema de estrategias:
- **Desktop**: 256MB heap, todas las características
- **Server**: 1GB heap, optimizado para alto rendimiento
- **IoT**: 16MB heap, eficiente en energía
- **Embedded**: 4MB heap, huella mínima
