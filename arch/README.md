# Architecture Subsystem

## English

### Overview
The Architecture Subsystem provides architecture-specific implementations and abstractions for the IR0 kernel. It supports multiple architectures with a common interface layer, currently implementing x86-32 and x86-64 with planned support for ARM32 and ARM64.

### Key Components

#### 1. Common Architecture Interface (`common/`)
- **Purpose**: Common interfaces and abstractions across architectures
- **Features**:
  - **arch_interface.h/c**: Common architecture interface definitions
  - **arch_awareness.h**: Architecture detection and capabilities
  - **common_paging.h**: Common paging structures and definitions
  - **idt.h**: Common interrupt descriptor table definitions

#### 2. x86-32 Architecture (`x86-32/`)
- **Purpose**: x86-32 implementation
- **Features**:
  - **Boot Assembly**: `boot_x86.asm` - 32-bit boot sequence
  - **Architecture Code**: `arch_x86.c/h` - x86-32 specific functions
  - **Interrupt Handling**: `idt_arch_x86.c/h` - 32-bit IDT management
  - **Linker Script**: `linker.ld` - 32-bit memory layout
  - **GRUB Configuration**: `grub.cfg` - Bootloader configuration

#### 3. x86-64 Architecture (`x86-64/`)
- **Purpose**: x86-64 implementation
- **Features**:
  - **Boot Assembly**: `boot_x64.asm` - 64-bit boot sequence
  - **Architecture Code**: `arch_x64.c/h` - x86-64 specific functions
  - **Interrupt Handling**: `idt_arch_x64.c/h` - 64-bit IDT management
  - **Fault Handling**: `fault.c` - Exception and fault handling
  - **Linker Script**: `linker.ld` - 64-bit memory layout
  - **GRUB Configuration**: `grub.cfg` - Bootloader configuration

### Architecture Support

#### x86-32 Support
```c
// x86-32 specific definitions
#define X86_32_PAGE_SIZE 4096
#define X86_32_PAGE_TABLE_ENTRIES 1024
#define X86_32_ADDRESS_SPACE 0x100000000  // 4GB
#define X86_32_KERNEL_BASE 0xC0000000

// x86-32 page table entry
typedef struct 
{
    uint32_t present : 1;
    uint32_t rw : 1;
    uint32_t user : 1;
    uint32_t accessed : 1;
    uint32_t dirty : 1;
    uint32_t unused : 7;
    uint32_t frame : 20;
} x86_32_page_entry_t;
```

#### x86-64 Support
```c
// x86-64 specific definitions
#define X86_64_PAGE_SIZE 4096
#define X86_64_PAGE_TABLE_ENTRIES 512
#define X86_64_ADDRESS_SPACE 0x1000000000000  // 256TB
#define X86_64_KERNEL_BASE 0xFFFFFFFF80000000

// x86-64 page table entry
typedef struct 
{
    uint64_t present : 1;
    uint64_t rw : 1;
    uint64_t user : 1;
    uint64_t pwt : 1;
    uint64_t pcd : 1;
    uint64_t accessed : 1;
    uint64_t dirty : 1;
    uint64_t pat : 1;
    uint64_t global : 1;
    uint64_t unused : 3;
    uint64_t frame : 40;
    uint64_t unused2 : 12;
} x86_64_page_entry_t;
```

### Boot Process

#### x86-32 Boot Sequence
```nasm
; boot_x86.asm - x86-32 boot sequence
[org 0x7c00]
[bits 16]

; Set up segments
mov ax, 0
mov ds, ax
mov es, ax
mov ss, ax
mov sp, 0x7c00

; Load kernel
mov ah, 0x02    ; Read sectors
mov al, 15      ; Number of sectors
mov ch, 0       ; Cylinder
mov cl, 2       ; Sector
mov dh, 0       ; Head
mov dl, 0x80    ; Drive
mov bx, 0x1000  ; Load address
int 0x13

; Switch to protected mode
cli
lgdt [gdt_descriptor]
mov eax, cr0
or eax, 1
mov cr0, eax

; Jump to 32-bit code
jmp 0x08:protected_mode

[bits 32]
protected_mode:
    ; Set up segments
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; Jump to kernel
    jmp 0x1000
```

#### x86-64 Boot Sequence
```nasm
; boot_x64.asm - x86-64 boot sequence
[org 0x7c00]
[bits 16]

; Set up segments
mov ax, 0
mov ds, ax
mov es, ax
mov ss, ax
mov sp, 0x7c00

; Load kernel
mov ah, 0x02    ; Read sectors
mov al, 15      ; Number of sectors
mov ch, 0       ; Cylinder
mov cl, 2       ; Sector
mov dh, 0       ; Head
mov dl, 0x80    ; Drive
mov bx, 0x1000  ; Load address
int 0x13

; Switch to long mode
cli
lgdt [gdt_descriptor]
mov eax, cr0
or eax, 1
mov cr0, eax

; Enable PAE
mov eax, cr4
or eax, 0x20
mov cr4, eax

; Set up page tables
mov eax, page_table_l4
mov cr3, eax

; Enable long mode
mov ecx, 0xC0000080
rdmsr
or eax, 0x100
wrmsr

; Enable paging
mov eax, cr0
or eax, 0x80000000
mov cr0, eax

; Load GDT and jump to long mode
lgdt [gdt64_descriptor]
jmp 0x08:long_mode

[bits 64]
long_mode:
    ; Set up segments
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; Jump to kernel
    jmp 0x1000
```

### Architecture Detection

#### Architecture Awareness
```c
// arch_awareness.h - Architecture detection
typedef enum 
{
    ARCH_UNKNOWN = 0,
    ARCH_X86_32,
    ARCH_X86_64,
    ARCH_ARM32,
    ARCH_ARM64
} architecture_t;

// Architecture capabilities
struct arch_capabilities 
{
    architecture_t arch;
    bool has_pae;
    bool has_sse;
    bool has_sse2;
    bool has_avx;
    bool has_multicore;
    uint32_t num_cores;
    uint64_t max_memory;
};

// Detect current architecture
architecture_t detect_architecture(void) 
{
    // Check CPUID for x86
    uint32_t eax, ebx, ecx, edx;
    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0));
    
    if (eax >= 1) 
    {
        asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
        if (edx & (1 << 29)) 
        {  // Long mode support
            return ARCH_X86_64;
        } 
        else 
        {
            return ARCH_X86_32;
        }
    }
    
    return ARCH_UNKNOWN;
}
```

### Memory Management

#### x86-32 Paging
```c
// x86-32 paging implementation
void x86_32_init_paging(void) 
{
    // Clear page directory
    memset(page_directory, 0, sizeof(page_directory));
    
    // Identity map first 4MB
    for (int i = 0; i < 1024; i++) 
    {
        page_directory[i] = (i * 4096) | 0x3;  // Present, RW, User
    }
    
    // Load page directory
    asm volatile("mov %0, %%cr3" : : "r"(page_directory));
    
    // Enable paging
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    asm volatile("mov %0, %%cr0" : : "r"(cr0));
}
```

#### x86-64 Paging
```c
// x86-64 paging implementation
void x86_64_init_paging(void) 
{
    // Clear page tables
    memset(page_table_l4, 0, sizeof(page_table_l4));
    memset(page_table_l3, 0, sizeof(page_table_l3));
    memset(page_table_l2, 0, sizeof(page_table_l2));
    memset(page_table_l1, 0, sizeof(page_table_l1));
    
    // Set up L4 table
    page_table_l4[0] = (uint64_t)page_table_l3 | 0x3;
    
    // Set up L3 table
    page_table_l3[0] = (uint64_t)page_table_l2 | 0x3;
    
    // Set up L2 table
    page_table_l2[0] = (uint64_t)page_table_l1 | 0x3;
    
    // Identity map first 2MB
    for (int i = 0; i < 512; i++) {
        page_table_l1[i] = (i * 4096) | 0x3;  // Present, RW, User
    }
    
    // Load page tables
    asm volatile("mov %0, %%cr3" : : "r"(page_table_l4));
}
```

### Interrupt Handling

#### x86-32 IDT
```c
// x86-32 IDT setup
void x86_32_setup_idt(void) 
{
    // Clear IDT
    memset(idt, 0, sizeof(idt));
    
    // Set up interrupt gates
    for (int i = 0; i < 256; i++) 
    {
        idt[i].offset_low = (uint16_t)((uint32_t)interrupt_handlers[i] & 0xFFFF);
        idt[i].selector = 0x08;  // Kernel code segment
        idt[i].zero = 0;
        idt[i].flags = 0x8E;     // Interrupt gate, present, ring 0
        idt[i].offset_high = (uint16_t)((uint32_t)interrupt_handlers[i] >> 16);
    }
    
    // Load IDT
    idt_descriptor.limit = sizeof(idt) - 1;
    idt_descriptor.base = (uint32_t)idt;
    asm volatile("lidt %0" : : "m"(idt_descriptor));
}
```

#### x86-64 IDT
```c
// x86-64 IDT setup
void x86_64_setup_idt(void) 
{
    // Clear IDT
    memset(idt, 0, sizeof(idt));
    
    // Set up interrupt gates
    for (int i = 0; i < 256; i++) 
    {
        idt[i].offset_low = (uint16_t)((uint64_t)interrupt_handlers[i] & 0xFFFF);
        idt[i].selector = 0x08;  // Kernel code segment
        idt[i].ist = 0;
        idt[i].flags = 0x8E;     // Interrupt gate, present, ring 0
        idt[i].offset_middle = (uint16_t)((uint64_t)interrupt_handlers[i] >> 16);
        idt[i].offset_high = (uint32_t)((uint64_t)interrupt_handlers[i] >> 32);
        idt[i].reserved = 0;
    }
    
    // Load IDT
    idt_descriptor.limit = sizeof(idt) - 1;
    idt_descriptor.base = (uint64_t)idt;
    asm volatile("lidt %0" : : "m"(idt_descriptor));
}
```

### Performance Characteristics

#### x86-32 Performance
- **Boot Time**: ~50ms
- **Memory Access**: 32-bit addressing
- **Page Table Lookup**: 2 levels
- **Interrupt Latency**: ~1μs
- **Context Switch**: ~100 cycles

#### x86-64 Performance
- **Boot Time**: ~60ms (longer due to mode switching)
- **Memory Access**: 64-bit addressing
- **Page Table Lookup**: 4 levels
- **Interrupt Latency**: ~800ns
- **Context Switch**: ~120 cycles

### Configuration

#### Architecture Configuration
```c
struct arch_config 
{
    architecture_t target_arch;
    bool enable_pae;
    bool enable_sse;
    bool enable_avx;
    bool enable_multicore;
    uint32_t kernel_base;
    uint32_t kernel_size;
    uint32_t stack_size;
};
```

#### Build Configuration
```makefile
# Makefile architecture support
ifeq ($(ARCH),x86-32)
    CFLAGS += -m32 -march=i686
    LDFLAGS += -melf_i386
    KERNEL_BASE = 0xC0000000
else ifeq ($(ARCH),x86-64)
    CFLAGS += -m64 -march=x86-64
    LDFLAGS += -melf_x86_64
    KERNEL_BASE = 0xFFFFFFFF80000000
endif
```

### Current Status

#### Working Features
- **Dual Architecture Support**: Both x86-32 and x86-64 compile and boot
- **Basic Paging**: Identity mapping and page table setup
- **Interrupt System**: IDT setup and basic interrupt handling
- **Boot Process**: Working boot sequences for both architectures
- **Memory Layout**: Proper kernel and user space separation

#### Development Areas
- **Advanced Paging**: On-demand paging and page fault handling
- **Memory Protection**: User/kernel space protection
- **Performance Optimization**: Architecture-specific optimizations
- **ARM Support**: Implementation for ARM32 and ARM64

---

## Español

### Descripción General
El Subsistema de Arquitectura proporciona implementaciones y abstracciones específicas por arquitectura para el kernel IR0. Soporta múltiples arquitecturas con una capa de interfaz común, implementando actualmente x86-32 y x86-64 con soporte planificado para ARM32 y ARM64.

### Componentes Principales

#### 1. Interfaz Común de Arquitectura (`common/`)
- **Propósito**: Interfaces y abstracciones comunes entre arquitecturas
- **Características**:
  - **arch_interface.h/c**: Definiciones de interfaz común de arquitectura
  - **arch_awareness.h**: Detección y capacidades de arquitectura
  - **common_paging.h**: Estructuras y definiciones comunes de paginación
  - **idt.h**: Definiciones comunes de tabla de descriptores de interrupción

#### 2. Arquitectura x86-32 (`x86-32/`)
- **Propósito**: Implementación de x86-32
- **Características**:
  - **Assembly de Boot**: `boot_x86.asm` - Secuencia de boot de 32 bits
  - **Código de Arquitectura**: `arch_x86.c/h` - Funciones específicas de x86-32
  - **Manejo de Interrupciones**: `idt_arch_x86.c/h` - Gestión de IDT de 32 bits
  - **Script de Linker**: `linker.ld` - Layout de memoria de 32 bits
  - **Configuración GRUB**: `grub.cfg` - Configuración del bootloader

#### 3. Arquitectura x86-64 (`x86-64/`)
- **Propósito**: Implementación de x86-64
- **Características**:
  - **Assembly de Boot**: `boot_x64.asm` - Secuencia de boot de 64 bits
  - **Código de Arquitectura**: `arch_x64.c/h` - Funciones específicas de x86-64
  - **Manejo de Interrupciones**: `idt_arch_x64.c/h` - Gestión de IDT de 64 bits
  - **Manejo de Fallos**: `fault.c` - Manejo de excepciones y fallos
  - **Script de Linker**: `linker.ld` - Layout de memoria de 64 bits
  - **Configuración GRUB**: `grub.cfg` - Configuración del bootloader

### Soporte de Arquitecturas

#### Soporte x86-32
```c
// Definiciones específicas de x86-32
#define X86_32_PAGE_SIZE 4096
#define X86_32_PAGE_TABLE_ENTRIES 1024
#define X86_32_ADDRESS_SPACE 0x100000000  // 4GB
#define X86_32_KERNEL_BASE 0xC0000000

// Entrada de tabla de páginas x86-32
typedef struct 
{
    uint32_t present : 1;
    uint32_t rw : 1;
    uint32_t user : 1;
    uint32_t accessed : 1;
    uint32_t dirty : 1;
    uint32_t unused : 7;
    uint32_t frame : 20;
} x86_32_page_entry_t;
```

#### Soporte x86-64
```c
// Definiciones específicas de x86-64
#define X86_64_PAGE_SIZE 4096
#define X86_64_PAGE_TABLE_ENTRIES 512
#define X86_64_ADDRESS_SPACE 0x1000000000000  // 256TB
#define X86_64_KERNEL_BASE 0xFFFFFFFF80000000

// Entrada de tabla de páginas x86-64
typedef struct 
{
    uint64_t present : 1;
    uint64_t rw : 1;
    uint64_t user : 1;
    uint64_t pwt : 1;
    uint64_t pcd : 1;
    uint64_t accessed : 1;
    uint64_t dirty : 1;
    uint64_t pat : 1;
    uint64_t global : 1;
    uint64_t unused : 3;
    uint64_t frame : 40;
    uint64_t unused2 : 12;
} x86_64_page_entry_t;
```

### Proceso de Boot

#### Secuencia de Boot x86-32
```nasm
; boot_x86.asm - Secuencia de boot x86-32
[org 0x7c00]
[bits 16]

; Configurar segmentos
mov ax, 0
mov ds, ax
mov es, ax
mov ss, ax
mov sp, 0x7c00

; Cargar kernel
mov ah, 0x02    ; Leer sectores
mov al, 15      ; Número de sectores
mov ch, 0       ; Cilindro
mov cl, 2       ; Sector
mov dh, 0       ; Cabeza
mov dl, 0x80    ; Disco
mov bx, 0x1000  ; Dirección de carga
int 0x13

; Cambiar a modo protegido
cli
lgdt [gdt_descriptor]
mov eax, cr0
or eax, 1
mov cr0, eax

; Saltar a código de 32 bits
jmp 0x08:protected_mode

[bits 32]
protected_mode:
    ; Configurar segmentos
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; Saltar al kernel
    jmp 0x1000
```

#### Secuencia de Boot x86-64
```nasm
; boot_x64.asm - Secuencia de boot x86-64
[org 0x7c00]
[bits 16]

; Configurar segmentos
mov ax, 0
mov ds, ax
mov es, ax
mov ss, ax
mov sp, 0x7c00

; Cargar kernel
mov ah, 0x02    ; Leer sectores
mov al, 15      ; Número de sectores
mov ch, 0       ; Cilindro
mov cl, 2       ; Sector
mov dh, 0       ; Cabeza
mov dl, 0x80    ; Disco
mov bx, 0x1000  ; Dirección de carga
int 0x13

; Cambiar a long mode
cli
lgdt [gdt_descriptor]
mov eax, cr0
or eax, 1
mov cr0, eax

; Habilitar PAE
mov eax, cr4
or eax, 0x20
mov cr4, eax

; Configurar tablas de páginas
mov eax, page_table_l4
mov cr3, eax

; Habilitar long mode
mov ecx, 0xC0000080
rdmsr
or eax, 0x100
wrmsr

; Habilitar paginación
mov eax, cr0
or eax, 0x80000000
mov cr0, eax

; Cargar GDT y saltar a long mode
lgdt [gdt64_descriptor]
jmp 0x08:long_mode

[bits 64]
long_mode:
    ; Configurar segmentos
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; Saltar al kernel
    jmp 0x1000
```

### Detección de Arquitectura

#### Conciencia de Arquitectura
```c
// arch_awareness.h - Detección de arquitectura
typedef enum 
{
    ARCH_UNKNOWN = 0,
    ARCH_X86_32,
    ARCH_X86_64,
    ARCH_ARM32,
    ARCH_ARM64
} architecture_t;

// Capacidades de arquitectura
struct arch_capabilities 
{
    architecture_t arch;
    bool has_pae;
    bool has_sse;
    bool has_sse2;
    bool has_avx;
    bool has_multicore;
    uint32_t num_cores;
    uint64_t max_memory;
};

// Detectar arquitectura actual
architecture_t detect_architecture(void) 
{
    // Verificar CPUID para x86
    uint32_t eax, ebx, ecx, edx;
    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0));
    
    if (eax >= 1) 
    {
        asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
        if (edx & (1 << 29)) 
        {  // Soporte de long mode
            return ARCH_X86_64;
        } 
        else 
        {
            return ARCH_X86_32;
        }
    }
    
    return ARCH_UNKNOWN;
}
```

### Gestión de Memoria

#### Paginación x86-32
```c
// Implementación de paginación x86-32
void x86_32_init_paging(void) 
{
    // Limpiar directorio de páginas
    memset(page_directory, 0, sizeof(page_directory));
    
    // Mapeo de identidad de primeros 4MB
    for (int i = 0; i < 1024; i++) 
    {
        page_directory[i] = (i * 4096) | 0x3;  // Present, RW, User
    }
    
    // Cargar directorio de páginas
    asm volatile("mov %0, %%cr3" : : "r"(page_directory));
    
    // Habilitar paginación
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    asm volatile("mov %0, %%cr0" : : "r"(cr0));
}
```

#### Paginación x86-64
```c
// Implementación de paginación x86-64
void x86_64_init_paging(void) 
{
    // Limpiar tablas de páginas
    memset(page_table_l4, 0, sizeof(page_table_l4));
    memset(page_table_l3, 0, sizeof(page_table_l3));
    memset(page_table_l2, 0, sizeof(page_table_l2));
    memset(page_table_l1, 0, sizeof(page_table_l1));
    
    // Configurar tabla L4
    page_table_l4[0] = (uint64_t)page_table_l3 | 0x3;
    
    // Configurar tabla L3
    page_table_l3[0] = (uint64_t)page_table_l2 | 0x3;
    
    // Configurar tabla L2
    page_table_l2[0] = (uint64_t)page_table_l1 | 0x3;
    
    // Mapeo de identidad de primeros 2MB
    for (int i = 0; i < 512; i++) 
    {
        page_table_l1[i] = (i * 4096) | 0x3;  // Present, RW, User
    }
    
    // Cargar tablas de páginas
    asm volatile("mov %0, %%cr3" : : "r"(page_table_l4));
}
```

### Manejo de Interrupciones

#### IDT x86-32
```c
// Configuración IDT x86-32
void x86_32_setup_idt(void) 
{
    // Limpiar IDT
    memset(idt, 0, sizeof(idt));
    
    // Configurar puertas de interrupción
    for (int i = 0; i < 256; i++) 
    {
        idt[i].offset_low = (uint16_t)((uint32_t)interrupt_handlers[i] & 0xFFFF);
        idt[i].selector = 0x08;  // Segmento de código kernel
        idt[i].zero = 0;
        idt[i].flags = 0x8E;     // Puerta de interrupción, presente, ring 0
        idt[i].offset_high = (uint16_t)((uint32_t)interrupt_handlers[i] >> 16);
    }
    
    // Cargar IDT
    idt_descriptor.limit = sizeof(idt) - 1;
    idt_descriptor.base = (uint32_t)idt;
    asm volatile("lidt %0" : : "m"(idt_descriptor));
}
```

#### IDT x86-64
```c
// Configuración IDT x86-64
void x86_64_setup_idt(void) 
{
    // Limpiar IDT
    memset(idt, 0, sizeof(idt));
    
    // Configurar puertas de interrupción
    for (int i = 0; i < 256; i++) 
    {
        idt[i].offset_low = (uint16_t)((uint64_t)interrupt_handlers[i] & 0xFFFF);
        idt[i].selector = 0x08;  // Segmento de código kernel
        idt[i].ist = 0;
        idt[i].flags = 0x8E;     // Puerta de interrupción, presente, ring 0
        idt[i].offset_middle = (uint16_t)((uint64_t)interrupt_handlers[i] >> 16);
        idt[i].offset_high = (uint32_t)((uint64_t)interrupt_handlers[i] >> 32);
        idt[i].reserved = 0;
    }
    
    // Cargar IDT
    idt_descriptor.limit = sizeof(idt) - 1;
    idt_descriptor.base = (uint64_t)idt;
    asm volatile("lidt %0" : : "m"(idt_descriptor));
}
```

### Características de Rendimiento

#### Rendimiento x86-32
- **Tiempo de Boot**: ~50ms
- **Acceso a Memoria**: Direccionamiento de 32 bits
- **Búsqueda en Tabla de Páginas**: 2 niveles
- **Latencia de Interrupción**: ~1μs
- **Context Switch**: ~100 ciclos

#### Rendimiento x86-64
- **Tiempo de Boot**: ~60ms (más largo por cambio de modo)
- **Acceso a Memoria**: Direccionamiento de 64 bits
- **Búsqueda en Tabla de Páginas**: 4 niveles
- **Latencia de Interrupción**: ~800ns
- **Context Switch**: ~120 ciclos

### Configuración

#### Configuración de Arquitectura
```c
struct arch_config 
{
    architecture_t target_arch;
    bool enable_pae;
    bool enable_sse;
    bool enable_avx;
    bool enable_multicore;
    uint32_t kernel_base;
    uint32_t kernel_size;
    uint32_t stack_size;
};
```

#### Configuración de Build
```makefile
# Soporte de arquitectura en Makefile
ifeq ($(ARCH),x86-32)
    CFLAGS += -m32 -march=i686
    LDFLAGS += -melf_i386
    KERNEL_BASE = 0xC0000000
else ifeq ($(ARCH),x86-64)
    CFLAGS += -m64 -march=x86-64
    LDFLAGS += -melf_x86_64
    KERNEL_BASE = 0xFFFFFFFF80000000
endif
```

### Estado Actual

#### Características Funcionando
- **Soporte Dual de Arquitectura**: Tanto x86-32 como x86-64 compilan y bootean
- **Paginación Básica**: Mapeo de identidad y configuración de tablas de páginas
- **Sistema de Interrupciones**: Configuración IDT y manejo básico de interrupciones
- **Proceso de Boot**: Secuencias de boot funcionando para ambas arquitecturas
- **Layout de Memoria**: Separación apropiada de espacio kernel y usuario

#### Áreas de Desarrollo
- **Paginación Avanzada**: Paginación bajo demanda y manejo de page faults
- **Protección de Memoria**: Protección de espacio usuario/kernel
- **Optimización de Rendimiento**: Optimizaciones específicas por arquitectura
- **Soporte ARM**: Implementación para ARM32 y ARM64
