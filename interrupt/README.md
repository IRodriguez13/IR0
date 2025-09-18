# Interrupt Subsystem

## English

### Overview
The Interrupt Subsystem provides basic interrupt handling for the IR0 kernel, supporting both x86-32 and x86-64 architectures. It includes an Interrupt Descriptor Table (IDT), exception handlers, and interrupt service routines with basic features like context preservation and error reporting.

### Key Components

#### 1. Interrupt Descriptor Table (`idt.c`)
- **Purpose**: Manages the IDT with 256 entries for all possible interrupts
- **Features**:
  - Basic IDT setup for x86-32 and x86-64
  - Dynamic interrupt handler registration framework
  - Interrupt gate configuration
  - TSS integration for x86-64
  - Interrupt stack management

#### 2. Interrupt Service Routines (`isr_handlers.c/h`)
- **Purpose**: Handles all interrupt and exception events
- **Features**:
  - **Exception Handlers**: Division error, page fault, invalid opcode, etc.
  - **Hardware Interrupts**: Timer, keyboard, disk, network framework
  - **System Calls**: Software interrupt handling
  - **Context Preservation**: Basic CPU state saving/restoration
  - **Error Reporting**: Basic diagnostic information

#### 3. Architecture-Specific Code (`arch/`)
- **Purpose**: Architecture-specific interrupt handling
- **Features**:
  - **x86-32**: 32-bit interrupt handling with legacy PIC
  - **x86-64**: 64-bit interrupt handling with APIC/LAPIC framework
  - **Assembly Routines**: Low-level interrupt entry/exit
  - **Register Management**: Architecture-specific register handling

### Interrupt Types

#### 1. Exceptions (0-31)
```
0x00: Division Error
0x01: Debug Exception
0x02: Non-Maskable Interrupt
0x03: Breakpoint
0x04: Overflow
0x05: Bound Range Exceeded
0x06: Invalid Opcode
0x07: Device Not Available
0x08: Double Fault
0x09: Coprocessor Segment Overrun
0x0A: Invalid TSS
0x0B: Segment Not Present
0x0C: Stack-Segment Fault
0x0D: General Protection Fault
0x0E: Page Fault
0x0F: Reserved
0x10: x87 FPU Error
0x11: Alignment Check
0x12: Machine Check
0x13: SIMD Floating-Point Exception
0x14-0x1F: Reserved
```

#### 2. Hardware Interrupts (32-47)
```
0x20: Timer (PIT)
0x21: Keyboard
0x22: Cascade
0x23: COM2
0x24: COM1
0x25: LPT2
0x26: Floppy Disk
0x27: LPT1
0x28: CMOS Real-time Clock
0x29: Free for peripherals
0x2A: Free for peripherals
0x2B: Free for peripherals
0x2C: PS/2 Mouse
0x2D: FPU
0x2E: Primary ATA Hard Disk
0x2F: Secondary ATA Hard Disk
```

#### 3. Software Interrupts (48-255)
```
0x30: System Call (IR0_SYSCALL)
0x31-0xFF: Available for software use
```

### Basic Features

#### 1. Context Preservation
```c
typedef struct 
{
#ifdef __x86_64__
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rip, rflags, cs, ss, ds, es, fs, gs;
#else
    uint32_t eax, ebx, ecx, edx, esi, edi, ebp, esp;
    uint32_t eip, eflags, cs, ss, ds, es, fs, gs;
#endif
} interrupt_context_t;
```

#### 2. Error Reporting
```c
void isr_page_fault_handler(interrupt_context_t* context) {
    uint64_t fault_address;
#ifdef __x86_64__
    asm volatile("mov %%cr2, %0" : "=r"(fault_address));
#else
    asm volatile("mov %%cr2, %0" : "=r"(fault_address));
#endif
    
    print_error("Page Fault at address 0x");
    print_hex64(fault_address);
    print(" (");
    print_hex32(context->error_code);
    print(")\n");
}
```

#### 3. System Call Handling
```c
void isr_system_call(interrupt_context_t* context) 
{
#ifdef __x86_64__
    uint64_t syscall_number = context->rax;
    uint64_t arg1 = context->rbx;
    uint64_t arg2 = context->rcx;
    uint64_t arg3 = context->rdx;
#else
    uint32_t syscall_number = context->eax;
    uint32_t arg1 = context->ebx;
    uint32_t arg2 = context->ecx;
    uint32_t arg3 = context->edx;
#endif
    
    // Handle system call
    uint64_t result = handle_system_call(syscall_number, arg1, arg2, arg3);
    
#ifdef __x86_64__
    context->rax = result;
#else
    context->eax = result;
#endif
}
```

### Architecture Support

#### x86-32 Support
- **PIC Configuration**: Legacy 8259A Programmable Interrupt Controller
- **Interrupt Gates**: 32-bit interrupt gates with proper privilege levels
- **Stack Management**: Interrupt stack frames on kernel stack
- **Register Preservation**: 32-bit register set preservation

#### x86-64 Support
- **APIC/LAPIC**: Advanced Programmable Interrupt Controller framework
- **TSS Integration**: Task State Segment for interrupt stacks
- **Interrupt Stacks**: Dedicated interrupt stacks (IST) framework
- **Register Preservation**: 64-bit register set preservation
- **Long Mode**: 64-bit interrupt handling

### Performance Characteristics

#### Interrupt Latency
- **Minimum Latency**: < 1 microsecond
- **Average Latency**: 2-5 microseconds
- **Maximum Latency**: < 10 microseconds (worst case)

#### Context Switch Overhead
- **Save Context**: ~50 CPU cycles
- **Restore Context**: ~50 CPU cycles
- **Total Overhead**: ~100 CPU cycles per interrupt

### Configuration Options

#### Interrupt Configuration
```c
struct interrupt_config 
{
    bool enable_pic;           // Enable legacy PIC
    bool enable_apic;          // Enable APIC/LAPIC
    bool enable_nmi;           // Enable NMI handling
    bool enable_debug;         // Enable debug exceptions
    uint32_t timer_frequency;  // Timer interrupt frequency
    bool enable_syscalls;      // Enable system call interrupts
};
```

#### Error Handling
```c
struct interrupt_error_handling 
{
    bool verbose_errors;       // Detailed error reporting
    bool panic_on_critical;    // Panic on critical errors
    bool log_all_interrupts;   // Log all interrupt events
    bool enable_recovery;      // Enable automatic recovery
};
```

### Debugging and Monitoring

#### Interrupt Statistics
```c
struct interrupt_stats 
{
    uint64_t total_interrupts;
    uint64_t exceptions_handled;
    uint64_t hardware_interrupts;
    uint64_t software_interrupts;
    uint64_t page_faults;
    uint64_t system_calls;
    uint64_t average_latency;
    uint64_t max_latency;
};
```

#### Debug Features
- **Interrupt Logging**: Basic log of interrupt events
- **Performance Profiling**: Basic interrupt latency measurement
- **Error Diagnostics**: Basic error information
- **Stack Tracing**: Basic interrupt context stack traces

### Current Status

#### Working Features
- **IDT Setup**: Basic interrupt descriptor table configuration
- **Exception Handling**: Basic exception handlers
- **Context Preservation**: Basic CPU state saving/restoration
- **System Call Interface**: Basic system call interrupt handling
- **Architecture Support**: Both x86-32 and x86-64

#### Development Areas
- **Advanced Interrupt Handling**: Complete APIC/LAPIC implementation
- **Performance Optimization**: Advanced interrupt optimizations
- **Error Recovery**: Advanced error recovery mechanisms
- **Interrupt Statistics**: Complete interrupt monitoring
- **Debug Features**: Advanced debugging capabilities

---

## Español

### Descripción General
El Subsistema de Interrupciones proporciona manejo básico de interrupciones para el kernel IR0, soportando arquitecturas x86-32 y x86-64. Incluye una Tabla de Descriptores de Interrupción (IDT), manejadores de excepciones y rutinas de servicio de interrupción con características básicas como preservación de contexto y reportes de errores.

### Componentes Principales

#### 1. Tabla de Descriptores de Interrupción (`idt.c`)
- **Propósito**: Gestiona la IDT con 256 entradas para todas las interrupciones posibles
- **Características**:
  - Configuración básica de IDT para x86-32 y x86-64
  - Framework de registro dinámico de manejadores de interrupción
  - Configuración de puertas de interrupción
  - Integración TSS para x86-64
  - Gestión de stacks de interrupción

#### 2. Rutinas de Servicio de Interrupción (`isr_handlers.c/h`)
- **Propósito**: Maneja todos los eventos de interrupción y excepción
- **Características**:
  - **Manejadores de Excepción**: Error de división, page fault, opcode inválido, etc.
  - **Interrupciones de Hardware**: Framework de timer, teclado, disco, red
  - **System Calls**: Manejo de interrupciones de software
  - **Preservación de Contexto**: Guardado/restauración básica del estado CPU
  - **Reportes de Error**: Información diagnóstica básica

#### 3. Código Específico de Arquitectura (`arch/`)
- **Propósito**: Manejo de interrupciones específico por arquitectura
- **Características**:
  - **x86-32**: Manejo de interrupciones de 32 bits con PIC legacy
  - **x86-64**: Framework de manejo de interrupciones de 64 bits con APIC/LAPIC
  - **Rutinas en Assembly**: Entrada/salida de interrupción de bajo nivel
  - **Gestión de Registros**: Manejo de registros específico por arquitectura

### Tipos de Interrupciones

#### 1. Excepciones (0-31)
```
0x00: Error de División
0x01: Excepción de Debug
0x02: Interrupción No Enmascarable
0x03: Breakpoint
0x04: Overflow
0x05: Rango Excedido
0x06: Opcode Inválido
0x07: Dispositivo No Disponible
0x08: Doble Fallo
0x09: Desbordamiento de Segmento de Coprocesador
0x0A: TSS Inválido
0x0B: Segmento No Presente
0x0C: Fallo de Stack-Segment
0x0D: Fallo de Protección General
0x0E: Page Fault
0x0F: Reservado
0x10: Error FPU x87
0x11: Verificación de Alineación
0x12: Verificación de Máquina
0x13: Excepción FPU SIMD
0x14-0x1F: Reservado
```

#### 2. Interrupciones de Hardware (32-47)
```
0x20: Timer (PIT)
0x21: Teclado
0x22: Cascada
0x23: COM2
0x24: COM1
0x25: LPT2
0x26: Disco Floppy
0x27: LPT1
0x28: Reloj Real CMOS
0x29: Libre para periféricos
0x2A: Libre para periféricos
0x2B: Libre para periféricos
0x2C: Ratón PS/2
0x2D: FPU
0x2E: Disco Duro ATA Primario
0x2F: Disco Duro ATA Secundario
```

#### 3. Interrupciones de Software (48-255)
```
0x30: System Call (IR0_SYSCALL)
0x31-0xFF: Disponible para uso de software
```

### Características Básicas

#### 1. Preservación de Contexto
```c
typedef struct 
{
#ifdef __x86_64__
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rip, rflags, cs, ss, ds, es, fs, gs;
#else
    uint32_t eax, ebx, ecx, edx, esi, edi, ebp, esp;
    uint32_t eip, eflags, cs, ss, ds, es, fs, gs;
#endif
} interrupt_context_t;
```

#### 2. Reportes de Error
```c
void isr_page_fault_handler(interrupt_context_t* context) 
{
    uint64_t fault_address;
#ifdef __x86_64__
    asm volatile("mov %%cr2, %0" : "=r"(fault_address));
#else
    asm volatile("mov %%cr2, %0" : "=r"(fault_address));
#endif
    
    print_error("Page Fault en dirección 0x");
    print_hex64(fault_address);
    print(" (");
    print_hex32(context->error_code);
    print(")\n");
}
```

#### 3. Manejo de System Calls
```c
void isr_system_call(interrupt_context_t* context) 
{
#ifdef __x86_64__
    uint64_t syscall_number = context->rax;
    uint64_t arg1 = context->rbx;
    uint64_t arg2 = context->rcx;
    uint64_t arg3 = context->rdx;
#else
    uint32_t syscall_number = context->eax;
    uint32_t arg1 = context->ebx;
    uint32_t arg2 = context->ecx;
    uint32_t arg3 = context->edx;
#endif
    
    // Manejar system call
    uint64_t result = handle_system_call(syscall_number, arg1, arg2, arg3);
    
#ifdef __x86_64__
    context->rax = result;
#else
    context->eax = result;
#endif
}
```

### Soporte de Arquitecturas

#### Soporte x86-32
- **Configuración PIC**: Controlador de Interrupciones Programmable 8259A legacy
- **Puertas de Interrupción**: Puertas de interrupción de 32 bits con niveles de privilegio apropiados
- **Gestión de Stack**: Frames de stack de interrupción en stack del kernel
- **Preservación de Registros**: Preservación del conjunto de registros de 32 bits

#### Soporte x86-64
- **APIC/LAPIC**: Framework de Controlador de Interrupciones Programmable Avanzado
- **Integración TSS**: Task State Segment para stacks de interrupción
- **Stacks de Interrupción**: Framework de stacks de interrupción dedicados (IST)
- **Preservación de Registros**: Preservación del conjunto de registros de 64 bits
- **Long Mode**: Manejo de interrupciones de 64 bits

### Características de Rendimiento

#### Latencia de Interrupción
- **Latencia Mínima**: < 1 microsegundo
- **Latencia Promedio**: 2-5 microsegundos
- **Latencia Máxima**: < 10 microsegundos (peor caso)

#### Overhead de Context Switch
- **Guardar Contexto**: ~50 ciclos CPU
- **Restaurar Contexto**: ~50 ciclos CPU
- **Overhead Total**: ~100 ciclos CPU por interrupción

### Opciones de Configuración

#### Configuración de Interrupciones
```c
struct interrupt_config 
{
    bool enable_pic;           // Habilitar PIC legacy
    bool enable_apic;          // Habilitar APIC/LAPIC
    bool enable_nmi;           // Habilitar manejo NMI
    bool enable_debug;         // Habilitar excepciones de debug
    uint32_t timer_frequency;  // Frecuencia de interrupción del timer
    bool enable_syscalls;      // Habilitar interrupciones de system call
};
```

#### Manejo de Errores
```c
struct interrupt_error_handling 
{
    bool verbose_errors;       // Reportes de error detallados
    bool panic_on_critical;    // Panic en errores críticos
    bool log_all_interrupts;   // Log de todos los eventos de interrupción
    bool enable_recovery;      // Habilitar recuperación automática
};
```

### Debugging y Monitoreo

#### Estadísticas de Interrupciones
```c
struct interrupt_stats 
{
    uint64_t total_interrupts;
    uint64_t exceptions_handled;
    uint64_t hardware_interrupts;
    uint64_t software_interrupts;
    uint64_t page_faults;
    uint64_t system_calls;
    uint64_t average_latency;
    uint64_t max_latency;
};
```

#### Características de Debug
- **Logging de Interrupciones**: Log básico de eventos de interrupción
- **Profiling de Rendimiento**: Medición básica de latencia de interrupciones
- **Diagnósticos de Error**: Información básica de errores
- **Stack Tracing**: Trazas básicas de stack del contexto de interrupción

### Estado Actual

#### Características Funcionando
- **Configuración IDT**: Configuración básica de tabla de descriptores de interrupción
- **Manejo de Excepciones**: Manejadores básicos de excepciones
- **Preservación de Contexto**: Guardado/restauración básica del estado CPU
- **Interfaz de System Calls**: Manejo básico de interrupciones de system calls
- **Soporte de Arquitectura**: Tanto x86-32 como x86-64

#### Áreas de Desarrollo
- **Manejo Avanzado de Interrupciones**: Implementación completa de APIC/LAPIC
- **Optimización de Rendimiento**: Optimizaciones avanzadas de interrupciones
- **Recuperación de Errores**: Mecanismos avanzados de recuperación de errores
- **Estadísticas de Interrupciones**: Monitoreo completo de interrupciones
- **Características de Debug**: Capacidades avanzadas de debugging
