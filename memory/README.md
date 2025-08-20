# Memory Management Subsystem

## English

### Overview
The Memory Management Subsystem provides basic memory management functionality for the IR0 kernel, including physical memory allocation, virtual memory management, heap allocation, and process memory isolation. It supports both x86-32 and x86-64 architectures with fundamental memory management features.

### Key Components

#### 1. Physical Memory Allocator (`physical_allocator.c/h`)
- **Purpose**: Manages physical memory pages using a bitmap-based allocation system
- **Features**:
  - Bitmap-based tracking for basic allocation/deallocation
  - Support for 4KB page sizes
  - Basic memory zone management
  - Simple memory coalescing
  - Basic statistics and debugging information

#### 2. Heap Allocator (`heap_allocator.c/h`)
- **Purpose**: Provides dynamic memory allocation for the kernel (kmalloc/kfree)
- **Features**:
  - Basic allocation pools for different sizes
  - Simple fragmentation management
  - Memory leak detection framework
  - Basic performance optimization
  - Debug information and statistics

#### 3. Virtual Memory Allocator (`vallocator.c`)
- **Purpose**: Manages virtual memory spaces and process memory isolation
- **Features**:
  - Virtual address space allocation
  - Basic process memory isolation
  - Memory protection (read, write, execute)
  - Simple shared memory support
  - Memory mapping and unmapping

#### 4. On-Demand Paging (`ondemand-paging.c/h`)
- **Purpose**: Framework for demand paging implementation
- **Features**:
  - Basic page fault handling structure
  - Memory pressure management framework
  - Page replacement algorithm interface
  - Memory compression support structure

#### 5. Process Memory Management (`process_memo.h`)
- **Purpose**: Defines structures and interfaces for process memory management
- **Features**:
  - Process memory space definitions
  - Memory layout specifications
  - Process memory isolation framework
  - Memory sharing between processes structure

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
- PAE support framework

#### x86-64 Support (`arch/x86-64/`)
- 4KB pages
- 256TB virtual address space
- 64-bit physical addressing
- Basic paging features

### Key Features

1. **Multi-Architecture Support**
   - Native support for x86-32 and x86-64
   - Architecture-specific optimizations
   - Conditional compilation for different platforms

2. **Basic Memory Management**
   - Identity mapping for kernel stability
   - Memory verification framework
   - Basic memory mapping validation
   - Error handling and recovery

3. **Performance Optimizations**
   - Efficient bitmap-based allocation
   - Basic allocation pools
   - Memory coalescing
   - Cache-friendly data structures

4. **Debugging and Monitoring**
   - Memory usage statistics
   - Leak detection framework
   - Performance profiling structure
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
- **Desktop**: 256MB heap, basic features
- **Server**: 1GB heap, optimized for high throughput
- **IoT**: 16MB heap, power-efficient
- **Embedded**: 4MB heap, minimal footprint

### Current Status

#### Working Features
- **Physical Memory Allocation**: Basic bitmap-based allocation
- **Heap Management**: kmalloc/kfree with basic pools
- **Virtual Memory**: Basic virtual address space management
- **Memory Layout**: Proper kernel and user space separation
- **Architecture Support**: Both x86-32 and x86-64

#### Development Areas
- **On-Demand Paging**: Complete page fault handling
- **Memory Protection**: Advanced user/kernel protection
- **Memory Compression**: Actual compression implementation
- **Performance Optimization**: Advanced allocation strategies

---

## Español

### Descripción General
El Subsistema de Gestión de Memoria proporciona funcionalidad básica de gestión de memoria para el kernel IR0, incluyendo asignación de memoria física, gestión de memoria virtual, asignación de heap y aislamiento de memoria por proceso. Soporta arquitecturas x86-32 y x86-64 con características fundamentales de gestión de memoria.

### Componentes Principales

#### 1. Asignador de Memoria Física (`physical_allocator.c/h`)
- **Propósito**: Gestiona páginas de memoria física usando un sistema de asignación basado en bitmap
- **Características**:
  - Seguimiento basado en bitmap para asignación/liberación básica
  - Soporte para tamaños de página de 4KB
  - Gestión básica de zonas de memoria
  - Coalescencia simple de memoria
  - Estadísticas básicas e información de debug

#### 2. Asignador de Heap (`heap_allocator.c/h`)
- **Propósito**: Proporciona asignación dinámica de memoria para el kernel (kmalloc/kfree)
- **Características**:
  - Pools básicos de asignación para diferentes tamaños
  - Gestión simple de fragmentación
  - Framework de detección de memory leaks
  - Optimización básica de rendimiento
  - Información de debug y estadísticas

#### 3. Asignador de Memoria Virtual (`vallocator.c`)
- **Propósito**: Gestiona espacios de memoria virtual y aislamiento de memoria por proceso
- **Características**:
  - Asignación de espacios de direcciones virtuales
  - Aislamiento básico de memoria por proceso
  - Protección de memoria (lectura, escritura, ejecución)
  - Soporte simple para memoria compartida
  - Mapeo y desmapeo de memoria

#### 4. Paginación Bajo Demanda (`ondemand-paging.c/h`)
- **Propósito**: Framework para implementación de paginación bajo demanda
- **Características**:
  - Estructura básica de manejo de page faults
  - Framework de gestión de presión de memoria
  - Interfaz de algoritmo de reemplazo de páginas
  - Estructura de soporte para compresión de memoria

#### 5. Gestión de Memoria de Procesos (`process_memo.h`)
- **Propósito**: Define estructuras e interfaces para gestión de memoria de procesos
- **Características**:
  - Definiciones de espacios de memoria de procesos
  - Especificaciones de layout de memoria
  - Framework de aislamiento de memoria por proceso
  - Estructura de compartición de memoria entre procesos

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
- Framework de soporte PAE

#### Soporte x86-64 (`arch/x86-64/`)
- Páginas de 4KB
- Espacio de direcciones virtual de 256TB
- Direccionamiento físico de 64 bits
- Características básicas de paginación

### Características Principales

1. **Soporte Multi-Arquitectura**
   - Soporte nativo para x86-32 y x86-64
   - Optimizaciones específicas por arquitectura
   - Compilación condicional para diferentes plataformas

2. **Gestión Básica de Memoria**
   - Mapeo de identidad para estabilidad del kernel
   - Framework de verificación de memoria
   - Validación básica de mapeo de memoria
   - Manejo de errores y recuperación

3. **Optimizaciones de Rendimiento**
   - Asignación eficiente basada en bitmap
   - Pools básicos de asignación
   - Coalescencia de memoria
   - Estructuras de datos cache-friendly

4. **Debugging y Monitoreo**
   - Estadísticas de uso de memoria
   - Framework de detección de leaks
   - Estructura de profiling de rendimiento
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
- **Desktop**: 256MB heap, características básicas
- **Server**: 1GB heap, optimizado para alto rendimiento
- **IoT**: 16MB heap, eficiente en energía
- **Embedded**: 4MB heap, huella mínima

### Estado Actual

#### Características Funcionando
- **Asignación de Memoria Física**: Asignación básica basada en bitmap
- **Gestión de Heap**: kmalloc/kfree con pools básicos
- **Memoria Virtual**: Gestión básica de espacios de direcciones virtuales
- **Layout de Memoria**: Separación apropiada de espacio kernel y usuario
- **Soporte de Arquitectura**: Tanto x86-32 como x86-64

#### Áreas de Desarrollo
- **Paginación Bajo Demanda**: Manejo completo de page faults
- **Protección de Memoria**: Protección avanzada usuario/kernel
- **Compresión de Memoria**: Implementación real de compresión
- **Optimización de Rendimiento**: Estrategias avanzadas de asignación
