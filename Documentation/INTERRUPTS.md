# IR0 Kernel Interrupt System

## Overview

The interrupt subsystem handles hardware interrupts, CPU exceptions, and system calls. It provides the foundation for hardware event handling, error recovery, and user-kernel communication. The system maps CPU exceptions to POSIX signals to prevent system hangs.

## Architecture

The interrupt system consists of:

1. **IDT (Interrupt Descriptor Table)** - Maps interrupt vectors to handlers
2. **PIC (Programmable Interrupt Controller)** - Routes hardware interrupts
3. **ISR Handlers** - Interrupt Service Routines for exception/interrupt handling
4. **Signal Integration** - Maps CPU exceptions to process signals

## IDT (Interrupt Descriptor Table)

### Overview

The IDT maps 256 interrupt vectors (0-255) to handler functions. Each entry contains:
- Offset to handler function (64-bit address split across multiple fields)
- Code segment selector
- Interrupt gate flags (privilege level, present bit)
- IST (Interrupt Stack Table) index (optional)

### Location

- Header: `interrupt/arch/idt.h`
- Implementation: `interrupt/arch/idt.c`
- Assembly stubs: `interrupt/arch/x86-64/isr_stubs_64.asm`

### IDT Structure (x86-64)

```c
struct idt_entry64 {
    uint16_t offset_low;    /* Lower 16 bits of handler address */
    uint16_t selector;      /* Code segment selector */
    uint8_t ist;            /* Interrupt Stack Table index */
    uint8_t flags;          /* Gate type, DPL, present */
    uint16_t offset_mid;    /* Middle 16 bits of handler address */
    uint32_t offset_high;   /* Upper 32 bits of handler address */
    uint32_t reserved;      /* Reserved (must be 0) */
} __attribute__((packed));
```

### IDT Entries

- **0-31:** CPU exceptions (divide by zero, page fault, etc.)
- **32-47:** Hardware interrupts (IRQs 0-15 via PIC)
- **128 (0x80):** System call interface
- **48-127, 129-255:** Unused/reserved

### Initialization

```c
void idt_init64(void);
void idt_load64(void);
```

**Setup Process:**
1. Initialize all 256 IDT entries
2. Set up exception handlers (0-31)
3. Set up interrupt handlers for PIC (32-47)
4. Set up syscall handler (128)
5. Load IDT register (LIDT instruction)

## PIC (Programmable Interrupt Controller)

### Overview

The 8259 PIC routes hardware interrupts (IRQs) to CPU interrupt vectors. IR0 uses two PICs in cascade mode (master + slave).

### Location

- Header: `interrupt/arch/pic.h`
- Implementation: `interrupt/arch/pic.c`

### PIC Configuration

**Master PIC (PIC1):**
- I/O Ports: 0x20 (command), 0x21 (data)
- IRQs 0-7 → Interrupt vectors 32-39

**Slave PIC (PIC2):**
- I/O Ports: 0xA0 (command), 0xA1 (data)
- IRQs 8-15 → Interrupt vectors 40-47
- Cascaded through IRQ 2 on master

### Remapping

```c
void pic_remap64(void);
```

**Remapping Process:**
- PICs are remapped from default vectors (0-15) to 32-47
- Prevents conflicts with CPU exception vectors (0-31)
- Enables hardware interrupts to work with IDT

### IRQ Management

```c
void pic_unmask_irq(uint8_t irq);  /* Enable specific IRQ */
void pic_mask_irq(uint8_t irq);    /* Disable specific IRQ */
void pic_send_eoi64(uint8_t irq);  /* End of Interrupt signal */
```

**Common IRQs:**
- IRQ 0: Timer (PIT)
- IRQ 1: Keyboard (PS/2)
- IRQ 2: Cascade (PIC2)
- IRQ 11: Network (RTL8139)
- IRQ 14: ATA Primary

### End of Interrupt (EOI)

Hardware interrupts must send EOI to PIC after handling:
- Master PIC: Send EOI to PIC1
- Slave PIC: Send EOI to both PIC2 and PIC1 (cascade)

## Interrupt Service Routines (ISR)

### Assembly Stubs

Location: `interrupt/arch/x86-64/isr_stubs_64.asm`

**Purpose:** Assembly entry points that:
1. Save CPU state (registers)
2. Call C handler function
3. Send EOI to PIC (for hardware interrupts)
4. Restore CPU state
5. Return from interrupt (IRETQ)

**Structure:**
- One stub per interrupt vector (isr0_64 through isr255_64)
- All stubs follow same pattern
- Stubs are placed in IDT entries

### C Handlers

Location: `interrupt/arch/isr_handlers.c`

**Main Handler:**
```c
void isr_handler64(uint64_t interrupt_number);
```

**Exception Handling (0-31):**
1. Maps CPU exception to POSIX signal
2. Sends signal to current process (if exists)
3. Signal handled by scheduler on next context switch
4. If no process context, prints error and continues

**Hardware Interrupt Handling (32-47):**
1. Routes to appropriate device handler
2. Sends EOI to PIC
3. Device-specific processing

**System Call Handling (128):**
1. Routes to syscall dispatcher
2. Processes system call
3. Returns result to user process

## CPU Exception Mapping

CPU exceptions are automatically mapped to POSIX signals:

| Exception # | Exception Name | Signal | Description |
|-------------|---------------|--------|-------------|
| 0 | Divide by Zero | SIGFPE | Division or modulo by zero |
| 4 | Overflow | SIGFPE | INTO instruction overflow |
| 6 | Invalid Opcode | SIGILL | Invalid instruction |
| 8 | Double Fault | SIGSEGV | Cascading exceptions |
| 11 | Segment Not Present | SIGSEGV | Invalid segment access |
| 13 | General Protection Fault | SIGSEGV | Memory/privilege violation |
| 14 | Page Fault | SIGSEGV | Invalid page access |
| 16 | x87 FPU Error | SIGFPE | Floating-point exception |
| 19 | SIMD FPU Exception | SIGFPE | SIMD floating-point error |
| 5 | Debug Exception | SIGTRAP | Breakpoint/trace trap |

**Benefits:**
- Prevents system hangs on user program errors
- Allows process-level error handling
- Consistent with POSIX signal semantics

## Hardware Interrupt Handlers

### Timer Interrupt (IRQ 0)

Handled by PIT driver:
- Increments system tick counter
- Used for system uptime calculation
- Currently doesn't trigger preemptive scheduling

### Keyboard Interrupt (IRQ 1)

Handled by PS/2 keyboard driver:
- Reads scan code from keyboard
- Converts to ASCII character
- Stores in keyboard buffer
- User processes read from `/dev/console`

### Network Interrupt (IRQ 11)

Handled by RTL8139 driver:
- Receives network packets
- Processes Ethernet frames
- Routes to network stack (ARP, IP, ICMP)

### ATA Interrupt (IRQ 14)

Handled by ATA driver:
- Disk I/O completion
- Signals waiting processes

## System Call Interface

### Syscall Entry

**Vector:** 128 (0x80)
**Invocation:** `int 0x80` instruction or `syscall` instruction

**x86-64 Convention:**
- Syscall number in RAX
- Arguments in RDI, RSI, RDX, R10, R8, R9
- Return value in RAX

**Handler:** Routes to `syscall_handler()` in `kernel/syscalls.c`

## Signal Integration

The interrupt system integrates with the signal subsystem:

1. **Exception → Signal:** CPU exceptions trigger signals in user processes
2. **Synchronous Handling:** Signals processed before context switch
3. **Process Termination:** Fatal signals (SIGKILL, SIGSEGV) terminate processes
4. **Error Recovery:** Non-fatal signals can be handled by user programs

**Signal Flow:**
```
CPU Exception → isr_handler64() → send_signal() → 
signal_pending bitmask set → handle_signals() (in scheduler) → 
process termination/handling
```

## Interrupt Priorities

Interrupts are handled in priority order:

1. **CPU Exceptions (0-31)** - Highest priority, handled first
2. **System Calls (128)** - Medium priority
3. **Hardware Interrupts (32-47)** - Lower priority, can be masked

**Masking:**
- Interrupts can be disabled globally (`cli` instruction)
- Individual IRQs can be masked via PIC
- Exceptions cannot be masked

## Error Handling

**Exception in User Process:**
- Signal sent to process
- Process terminates or handles signal
- Kernel continues normally

**Exception in Kernel:**
- Kernel panic triggered
- System halts for debugging
- Error logged to serial

**Hardware Interrupt Failure:**
- Device handler logs error
- Interrupt cleared
- System continues (device may be unavailable)

## Implementation Notes

- **Single CPU:** No SMP-aware interrupt routing
- **No APIC:** Uses legacy PIC (8259)
- **Simple Routing:** Direct mapping, no interrupt routing tables
- **No Nested Interrupts:** Interrupts disabled during handling
- **No Interrupt Threads:** Handlers run in interrupt context

## Debugging

Interrupt-related information available via:
- `/proc/interrupts` - IRQ usage statistics
- Serial console - Exception/interrupt logs
- Kernel panic - Unhandled exceptions logged

---

# Sistema de Interrupciones del Kernel IR0

## Resumen

El subsistema de interrupciones maneja interrupciones de hardware, excepciones CPU y llamadas al sistema. Proporciona la base para manejo de eventos de hardware, recuperación de errores y comunicación usuario-kernel. El sistema mapea excepciones CPU a señales POSIX para prevenir cuelgues del sistema.

## Arquitectura

El sistema de interrupciones consta de:

1. **IDT (Interrupt Descriptor Table)** - Mapea vectores de interrupción a handlers
2. **PIC (Programmable Interrupt Controller)** - Enruta interrupciones de hardware
3. **Handlers ISR** - Rutinas de Servicio de Interrupción para manejo de excepciones/interrupciones
4. **Integración de Señales** - Mapea excepciones CPU a señales de proceso

## IDT (Interrupt Descriptor Table)

### Resumen

El IDT mapea 256 vectores de interrupción (0-255) a funciones handler. Cada entrada contiene:
- Offset a función handler (dirección de 64 bits dividida en múltiples campos)
- Selector de segmento de código
- Banderas de puerta de interrupción (nivel de privilegio, bit presente)
- Índice IST (Interrupt Stack Table) (opcional)

### Ubicación

- Header: `interrupt/arch/idt.h`
- Implementación: `interrupt/arch/idt.c`
- Stubs de ensamblador: `interrupt/arch/x86-64/isr_stubs_64.asm`

### Estructura IDT (x86-64)

```c
struct idt_entry64 {
    uint16_t offset_low;    /* 16 bits inferiores de dirección handler */
    uint16_t selector;      /* Selector de segmento de código */
    uint8_t ist;            /* Índice Interrupt Stack Table */
    uint8_t flags;          /* Tipo de puerta, DPL, presente */
    uint16_t offset_mid;    /* 16 bits medios de dirección handler */
    uint32_t offset_high;   /* 32 bits superiores de dirección handler */
    uint32_t reserved;      /* Reservado (debe ser 0) */
} __attribute__((packed));
```

### Entradas IDT

- **0-31:** Excepciones CPU (división por cero, fallo de página, etc.)
- **32-47:** Interrupciones de hardware (IRQs 0-15 via PIC)
- **128 (0x80):** Interfaz de llamadas al sistema
- **48-127, 129-255:** No usadas/reservadas

### Inicialización

```c
void idt_init64(void);
void idt_load64(void);
```

**Proceso de Configuración:**
1. Inicializa las 256 entradas IDT
2. Configura handlers de excepciones (0-31)
3. Configura handlers de interrupciones para PIC (32-47)
4. Configura handler de syscall (128)
5. Carga registro IDT (instrucción LIDT)

## PIC (Programmable Interrupt Controller)

### Resumen

El PIC 8259 enruta interrupciones de hardware (IRQs) a vectores de interrupción CPU. IR0 usa dos PICs en modo cascade (maestro + esclavo).

### Ubicación

- Header: `interrupt/arch/pic.h`
- Implementación: `interrupt/arch/pic.c`

### Configuración PIC

**PIC Maestro (PIC1):**
- Puertos I/O: 0x20 (comando), 0x21 (datos)
- IRQs 0-7 → Vectores de interrupción 32-39

**PIC Esclavo (PIC2):**
- Puertos I/O: 0xA0 (comando), 0xA1 (datos)
- IRQs 8-15 → Vectores de interrupción 40-47
- Conectado en cascade a través de IRQ 2 en maestro

### Remapeo

```c
void pic_remap64(void);
```

**Proceso de Remapeo:**
- PICs se remapean de vectores por defecto (0-15) a 32-47
- Previene conflictos con vectores de excepciones CPU (0-31)
- Habilita interrupciones de hardware para trabajar con IDT

### Gestión de IRQ

```c
void pic_unmask_irq(uint8_t irq);  /* Habilitar IRQ específico */
void pic_mask_irq(uint8_t irq);    /* Deshabilitar IRQ específico */
void pic_send_eoi64(uint8_t irq);  /* Señal End of Interrupt */
```

**IRQs Comunes:**
- IRQ 0: Timer (PIT)
- IRQ 1: Teclado (PS/2)
- IRQ 2: Cascade (PIC2)
- IRQ 11: Red (RTL8139)
- IRQ 14: ATA Primary

### End of Interrupt (EOI)

Las interrupciones de hardware deben enviar EOI al PIC después del manejo:
- PIC Maestro: Enviar EOI a PIC1
- PIC Esclavo: Enviar EOI tanto a PIC2 como PIC1 (cascade)

## Rutinas de Servicio de Interrupción (ISR)

### Stubs de Ensamblador

Ubicación: `interrupt/arch/x86-64/isr_stubs_64.asm`

**Propósito:** Puntos de entrada en ensamblador que:
1. Guardan estado CPU (registros)
2. Llaman función handler C
3. Envían EOI al PIC (para interrupciones de hardware)
4. Restauran estado CPU
5. Retornan de interrupción (IRETQ)

**Estructura:**
- Un stub por vector de interrupción (isr0_64 hasta isr255_64)
- Todos los stubs siguen el mismo patrón
- Stubs se colocan en entradas IDT

### Handlers C

Ubicación: `interrupt/arch/isr_handlers.c`

**Handler Principal:**
```c
void isr_handler64(uint64_t interrupt_number);
```

**Manejo de Excepciones (0-31):**
1. Mapea excepción CPU a señal POSIX
2. Envía señal al proceso actual (si existe)
3. Señal manejada por planificador en próximo cambio de contexto
4. Si no hay contexto de proceso, imprime error y continúa

**Manejo de Interrupciones de Hardware (32-47):**
1. Enruta a handler de dispositivo apropiado
2. Envía EOI al PIC
3. Procesamiento específico del dispositivo

**Manejo de Llamadas al Sistema (128):**
1. Enruta a despachador de syscall
2. Procesa llamada al sistema
3. Retorna resultado a proceso de usuario

## Mapeo de Excepciones CPU

Las excepciones CPU se mapean automáticamente a señales POSIX:

| Excepción # | Nombre de Excepción | Señal | Descripción |
|-------------|---------------------|-------|-------------|
| 0 | División por Cero | SIGFPE | División o módulo por cero |
| 4 | Overflow | SIGFPE | Overflow de instrucción INTO |
| 6 | Opcode Inválido | SIGILL | Instrucción inválida |
| 8 | Doble Fallo | SIGSEGV | Excepciones en cascada |
| 11 | Segmento No Presente | SIGSEGV | Acceso de segmento inválido |
| 13 | Fallo de Protección General | SIGSEGV | Violación de memoria/privilegio |
| 14 | Fallo de Página | SIGSEGV | Acceso de página inválido |
| 16 | Error FPU x87 | SIGFPE | Excepción de punto flotante |
| 19 | Excepción FPU SIMD | SIGFPE | Error de punto flotante SIMD |
| 5 | Excepción de Debug | SIGTRAP | Trampa de punto de interrupción/rastreo |

**Beneficios:**
- Previene cuelgues del sistema en errores de programas de usuario
- Permite manejo de errores a nivel de proceso
- Consistente con semántica de señales POSIX

## Handlers de Interrupciones de Hardware

### Interrupción de Timer (IRQ 0)

Manejado por driver PIT:
- Incrementa contador de ticks del sistema
- Usado para cálculo de tiempo de actividad del sistema
- Actualmente no dispara planificación preventiva

### Interrupción de Teclado (IRQ 1)

Manejado por driver de teclado PS/2:
- Lee código de escaneo del teclado
- Convierte a carácter ASCII
- Almacena en buffer de teclado
- Procesos de usuario leen desde `/dev/console`

### Interrupción de Red (IRQ 11)

Manejado por driver RTL8139:
- Recibe paquetes de red
- Procesa frames Ethernet
- Enruta a stack de red (ARP, IP, ICMP)

### Interrupción ATA (IRQ 14)

Manejado por driver ATA:
- Completación de I/O de disco
- Señaliza procesos en espera

## Interfaz de Llamadas al Sistema

### Entrada de Syscall

**Vector:** 128 (0x80)
**Invocación:** Instrucción `int 0x80` o instrucción `syscall`

**Convención x86-64:**
- Número de syscall en RAX
- Argumentos en RDI, RSI, RDX, R10, R8, R9
- Valor de retorno en RAX

**Handler:** Enruta a `syscall_handler()` en `kernel/syscalls.c`

## Integración de Señales

El sistema de interrupciones se integra con el subsistema de señales:

1. **Excepción → Señal:** Excepciones CPU disparan señales en procesos de usuario
2. **Manejo Síncrono:** Señales procesadas antes del cambio de contexto
3. **Terminación de Proceso:** Señales fatales (SIGKILL, SIGSEGV) terminan procesos
4. **Recuperación de Errores:** Señales no fatales pueden ser manejadas por programas de usuario

**Flujo de Señal:**
```
Excepción CPU → isr_handler64() → send_signal() → 
bitmask signal_pending establecido → handle_signals() (en planificador) → 
terminación/manejo de proceso
```

## Prioridades de Interrupción

Las interrupciones se manejan en orden de prioridad:

1. **Excepciones CPU (0-31)** - Mayor prioridad, manejadas primero
2. **Llamadas al Sistema (128)** - Prioridad media
3. **Interrupciones de Hardware (32-47)** - Prioridad menor, pueden ser enmascaradas

**Enmascaramiento:**
- Las interrupciones pueden deshabilitarse globalmente (instrucción `cli`)
- IRQs individuales pueden enmascararse via PIC
- Las excepciones no pueden enmascararse

## Manejo de Errores

**Excepción en Proceso de Usuario:**
- Señal enviada al proceso
- Proceso termina o maneja señal
- Kernel continúa normalmente

**Excepción en Kernel:**
- Se dispara panic del kernel
- Sistema se detiene para debugging
- Error registrado en serial

**Fallo de Interrupción de Hardware:**
- Handler de dispositivo registra error
- Interrupción limpiada
- Sistema continúa (dispositivo puede no estar disponible)

## Notas de Implementación

- **CPU Única:** Sin enrutamiento de interrupciones consciente de SMP
- **Sin APIC:** Usa PIC legado (8259)
- **Enrutamiento Simple:** Mapeo directo, sin tablas de enrutamiento de interrupciones
- **Sin Interrupciones Anidadas:** Interrupciones deshabilitadas durante manejo
- **Sin Hilos de Interrupción:** Handlers se ejecutan en contexto de interrupción

## Debugging

Información relacionada con interrupciones disponible via:
- `/proc/interrupts` - Estadísticas de uso de IRQ
- Consola serial - Registros de excepciones/interrupciones
- Kernel panic - Excepciones no manejadas registradas

