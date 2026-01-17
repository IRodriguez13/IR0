# IR0 Kernel Scheduling Subsystem

## Overview

The scheduling subsystem manages CPU time allocation among processes. IR0 currently implements a simple Round-Robin scheduler that provides fair time-sharing among all ready processes. The scheduler integrates with the process management and signal handling subsystems.

## Architecture

The scheduler is located in `kernel/rr_sched.c` and `kernel/rr_sched.h`. It uses:
- **Round-Robin Algorithm** - Simple time-slicing with circular queue
- **Process State Management** - Tracks READY, RUNNING, BLOCKED states
- **Context Switching** - Architecture-specific context save/restore
- **Signal Integration** - Handles signals before each context switch

## Round-Robin Scheduler

### Algorithm

The Round-Robin scheduler maintains a circular linked list of processes:

1. **Process Queue** - Linked list of `rr_task_t` nodes, each containing a `process_t*`
2. **Current Pointer** - Points to the currently executing process node
3. **Circular Selection** - Each context switch advances to the next node, wrapping to head when reaching tail

### Data Structures

```c
typedef struct rr_task {
    process_t *process;    /* Pointer to process structure */
    struct rr_task *next;  /* Next process in queue */
} rr_task_t;
```

**Global State:**
- `rr_head` - Head of the circular queue
- `rr_tail` - Tail of the circular queue
- `rr_current` - Current process node pointer

### Operations

#### Adding Processes

`rr_add_process(process_t *proc)` - Adds a process to the scheduler queue:

1. Validates process pointer
2. Allocates new `rr_task_t` node
3. Sets process state to PROCESS_READY
4. Appends to tail of queue
5. Updates tail pointer

#### Scheduling Next Process

`rr_schedule_next()` - Performs context switch to next process:

1. **Selection**: Advances `rr_current` to next node (wraps to head if at tail)
2. **State Update**: 
   - Previous process → PROCESS_READY (if was RUNNING)
   - Next process → PROCESS_RUNNING
   - Updates `current_process` global
3. **Signal Handling**: Calls `handle_signals()` before context switch
4. **Context Switch**:
   - First switch: Uses `jmp_ring3()` to jump to first user process
   - Subsequent switches: Uses `switch_context_x64()` for register save/restore

**First Context Switch:**
- Special handling for first user process
- Uses `jmp_ring3()` to transition from kernel to user mode (Ring 3)
- Never returns (process should only exit via syscall)

**Normal Context Switch:**
- Saves current process registers to `prev->task`
- Loads next process registers from `next->task`
- Switches page directory (CR3 register) for memory isolation
- Restores CPU state and resumes execution

## Context Switching

Context switching is architecture-specific. For x86-64:

### Assembly Implementation

Location: `kernel/scheduler/switch/switch_x64.asm`

**Saved Registers:**
- General purpose: RAX, RBX, RCX, RDX, RSI, RDI, R8-R15
- Stack: RSP, RBP
- Control: RIP, RFLAGS
- Segments: CS, DS, ES, FS, GS, SS
- Memory: CR3 (page directory address)

**Context Switch Process:**
1. Save all current process registers to `task_t` structure
2. Load CR3 with new process page directory (memory isolation)
3. Load segment registers for new process
4. Load general purpose registers
5. Load stack pointer (RSP, RBP)
6. Load RFLAGS
7. Prepare stack for IRETQ (interrupt return)
8. Execute IRETQ to jump to new process RIP

### Memory Isolation

Each process has its own page directory:
- **CR3 Register** - Points to process page directory (PML4 table for x86-64)
- **Kernel Space** - Shared identity mapping across all processes
- **User Space** - Process-specific virtual address space
- CR3 is updated during context switch to isolate process memory

## Process States and Scheduling

Processes must be in appropriate state to be scheduled:

- **PROCESS_READY** - Eligible for scheduling
- **PROCESS_RUNNING** - Currently executing (only one at a time)
- **PROCESS_BLOCKED** - Not eligible (waiting for I/O)
- **PROCESS_ZOMBIE** - Not eligible (terminated, awaiting cleanup)

**Scheduler Behavior:**
- Only PROCESS_READY processes are scheduled
- Processes are removed from scheduler when they exit (become ZOMBIE)
- Blocked processes remain in queue but are skipped

## Signal Integration

The scheduler calls `handle_signals()` before each context switch:

1. Checks for pending signals in current process
2. Handles signals in priority order:
   - SIGKILL (immediate termination)
   - CPU exceptions (SIGSEGV, SIGFPE, SIGILL, SIGBUS, SIGTRAP)
   - Termination signals (SIGTERM, SIGINT, SIGQUIT)
   - Process control signals (SIGSTOP, SIGCONT)
3. Clears signal bitmask after handling

This ensures signals are processed synchronously before process execution continues.

## Time Slicing

Current implementation:
- **No explicit time slicing** - Processes run until they:
  - Voluntarily yield (call syscall)
  - Block on I/O
  - Exit
  - Receive a signal that blocks/terminates them

**Future Enhancement:** Integration with timer interrupts for preemptive scheduling with time slices (e.g., 10ms per process).

## Scheduler Limitations

Current implementation has the following limitations:

1. **No Priorities** - All processes have equal priority
2. **No Nice Values** - Cannot adjust process priority
3. **Cooperative Multitasking** - Processes must yield voluntarily
4. **No Preemption** - Timer interrupts don't trigger context switches
5. **Simple Queue** - Linked list, not optimized for many processes
6. **No Load Balancing** - Single CPU only (no SMP support)

## Integration Points

The scheduler integrates with:

1. **Process Management** - Receives processes via `rr_add_process()`
2. **Memory Management** - Switches page directories (CR3)
3. **Signal System** - Calls `handle_signals()` before switches
4. **System Calls** - Processes yield via syscall interface
5. **Interrupt System** - Future: Timer interrupts for preemption

## Initialization

The scheduler is initialized automatically when first process is added:

1. `rr_head` and `rr_tail` start as NULL
2. First `rr_add_process()` creates initial queue
3. First `rr_schedule_next()` performs initial context switch to user mode

## Example Scheduling Flow

```
1. Process A added → rr_add_process(proc_a) → Queue: [A]
2. Process B added → rr_add_process(proc_b) → Queue: [A] → [B]
3. First schedule → rr_schedule_next() → jmp_ring3(proc_a) → A runs
4. A calls syscall → Returns to kernel → rr_schedule_next()
5. Context switch → switch_context_x64(A, B) → B runs
6. B calls syscall → Returns to kernel → rr_schedule_next()
7. Context switch → switch_context_x64(B, A) → A runs (round-robin)
```

## Future Enhancements

Potential improvements for the scheduler:

1. **CFS (Completely Fair Scheduler)** - Already implemented in `kernel/scheduler/` but not used
2. **Preemptive Scheduling** - Timer interrupt triggers context switch
3. **Process Priorities** - Priority-based scheduling
4. **Nice Values** - Adjustable process priority (-20 to +19)
5. **SMP Support** - Per-CPU scheduler queues for multiprocessor systems
6. **Load Balancing** - Distribute processes across CPUs

## Error Handling

The scheduler uses BUG_ON assertions for critical errors:

- Out of memory when allocating scheduler node
- Invalid process pointer
- Scheduler node without process pointer

All errors trigger kernel panic for immediate debugging.

---

# Subsistema de Planificación del Kernel IR0

## Resumen

El subsistema de planificación gestiona la asignación de tiempo de CPU entre procesos. IR0 implementa actualmente un planificador Round-Robin simple que proporciona tiempo compartido justo entre todos los procesos listos. El planificador se integra con los subsistemas de gestión de procesos y manejo de señales.

## Arquitectura

El planificador está ubicado en `kernel/rr_sched.c` y `kernel/rr_sched.h`. Utiliza:
- **Algoritmo Round-Robin** - Partición de tiempo simple con cola circular
- **Gestión de Estado de Proceso** - Rastrea estados READY, RUNNING, BLOCKED
- **Cambio de Contexto** - Guardado/restauración de contexto específico de arquitectura
- **Integración de Señales** - Maneja señales antes de cada cambio de contexto

## Planificador Round-Robin

### Algoritmo

El planificador Round-Robin mantiene una lista enlazada circular de procesos:

1. **Cola de Procesos** - Lista enlazada de nodos `rr_task_t`, cada uno contiene un `process_t*`
2. **Puntero Actual** - Apunta al nodo del proceso que se está ejecutando actualmente
3. **Selección Circular** - Cada cambio de contexto avanza al siguiente nodo, volviendo a la cabeza al llegar a la cola

### Estructuras de Datos

```c
typedef struct rr_task {
    process_t *process;    /* Puntero a estructura de proceso */
    struct rr_task *next;  /* Siguiente proceso en cola */
} rr_task_t;
```

**Estado Global:**
- `rr_head` - Cabeza de la cola circular
- `rr_tail` - Cola de la cola circular
- `rr_current` - Puntero al nodo del proceso actual

### Operaciones

#### Agregar Procesos

`rr_add_process(process_t *proc)` - Agrega un proceso a la cola del planificador:

1. Valida puntero de proceso
2. Asigna nuevo nodo `rr_task_t`
3. Establece estado del proceso a PROCESS_READY
4. Añade a la cola de la cola
5. Actualiza puntero de cola

#### Planificar Siguiente Proceso

`rr_schedule_next()` - Realiza cambio de contexto al siguiente proceso:

1. **Selección**: Avanza `rr_current` al siguiente nodo (vuelve a la cabeza si está en la cola)
2. **Actualización de Estado**: 
   - Proceso anterior → PROCESS_READY (si estaba RUNNING)
   - Siguiente proceso → PROCESS_RUNNING
   - Actualiza global `current_process`
3. **Manejo de Señales**: Llama `handle_signals()` antes del cambio de contexto
4. **Cambio de Contexto**:
   - Primer cambio: Usa `jmp_ring3()` para saltar al primer proceso de usuario
   - Cambios subsiguientes: Usa `switch_context_x64()` para guardado/restauración de registros

**Primer Cambio de Contexto:**
- Manejo especial para primer proceso de usuario
- Usa `jmp_ring3()` para transición de kernel a modo usuario (Ring 3)
- Nunca retorna (el proceso solo debe salir via syscall)

**Cambio de Contexto Normal:**
- Guarda registros del proceso actual en `prev->task`
- Carga registros del siguiente proceso desde `next->task`
- Cambia directorio de página (registro CR3) para aislamiento de memoria
- Restaura estado CPU y reanuda ejecución

## Cambio de Contexto

El cambio de contexto es específico de arquitectura. Para x86-64:

### Implementación en Ensamblador

Ubicación: `kernel/scheduler/switch/switch_x64.asm`

**Registros Guardados:**
- Propósito general: RAX, RBX, RCX, RDX, RSI, RDI, R8-R15
- Stack: RSP, RBP
- Control: RIP, RFLAGS
- Segmentos: CS, DS, ES, FS, GS, SS
- Memoria: CR3 (dirección del directorio de página)

**Proceso de Cambio de Contexto:**
1. Guarda todos los registros del proceso actual en estructura `task_t`
2. Carga CR3 con directorio de página del nuevo proceso (aislamiento de memoria)
3. Carga registros de segmento del nuevo proceso
4. Carga registros de propósito general
5. Carga puntero de stack (RSP, RBP)
6. Carga RFLAGS
7. Prepara stack para IRETQ (retorno de interrupción)
8. Ejecuta IRETQ para saltar al RIP del nuevo proceso

### Aislamiento de Memoria

Cada proceso tiene su propio directorio de página:
- **Registro CR3** - Apunta al directorio de página del proceso (tabla PML4 para x86-64)
- **Espacio del Kernel** - Mapeo idéntico compartido entre todos los procesos
- **Espacio de Usuario** - Espacio de direcciones virtuales específico del proceso
- CR3 se actualiza durante el cambio de contexto para aislar la memoria del proceso

## Estados de Proceso y Planificación

Los procesos deben estar en estado apropiado para ser planificados:

- **PROCESS_READY** - Elegible para planificación
- **PROCESS_RUNNING** - Ejecutándose actualmente (solo uno a la vez)
- **PROCESS_BLOCKED** - No elegible (esperando I/O)
- **PROCESS_ZOMBIE** - No elegible (terminado, esperando limpieza)

**Comportamiento del Planificador:**
- Solo se planifican procesos PROCESS_READY
- Los procesos se eliminan del planificador cuando terminan (se vuelven ZOMBIE)
- Los procesos bloqueados permanecen en cola pero se omiten

## Integración de Señales

El planificador llama `handle_signals()` antes de cada cambio de contexto:

1. Verifica señales pendientes en el proceso actual
2. Maneja señales en orden de prioridad:
   - SIGKILL (terminación inmediata)
   - Excepciones CPU (SIGSEGV, SIGFPE, SIGILL, SIGBUS, SIGTRAP)
   - Señales de terminación (SIGTERM, SIGINT, SIGQUIT)
   - Señales de control de proceso (SIGSTOP, SIGCONT)
3. Limpia máscara de bits de señales después del manejo

Esto asegura que las señales se procesen síncronamente antes de que continúe la ejecución del proceso.

## Partición de Tiempo

Implementación actual:
- **Sin partición de tiempo explícita** - Los procesos se ejecutan hasta que:
  - Ceden voluntariamente (llaman syscall)
  - Se bloquean en I/O
  - Salen
  - Reciben una señal que los bloquea/termina

**Mejora Futura:** Integración con interrupciones de timer para planificación preventiva con particiones de tiempo (ej: 10ms por proceso).

## Limitaciones del Planificador

La implementación actual tiene las siguientes limitaciones:

1. **Sin Prioridades** - Todos los procesos tienen igual prioridad
2. **Sin Valores Nice** - No se puede ajustar la prioridad del proceso
3. **Multitarea Cooperativa** - Los procesos deben ceder voluntariamente
4. **Sin Prevención** - Las interrupciones de timer no disparan cambios de contexto
5. **Cola Simple** - Lista enlazada, no optimizada para muchos procesos
6. **Sin Balanceo de Carga** - Solo una CPU (sin soporte SMP)

## Puntos de Integración

El planificador se integra con:

1. **Gestión de Procesos** - Recibe procesos via `rr_add_process()`
2. **Gestión de Memoria** - Cambia directorios de página (CR3)
3. **Sistema de Señales** - Llama `handle_signals()` antes de cambios
4. **Llamadas al Sistema** - Los procesos ceden via interfaz de syscall
5. **Sistema de Interrupciones** - Futuro: Interrupciones de timer para prevención

## Inicialización

El planificador se inicializa automáticamente cuando se agrega el primer proceso:

1. `rr_head` y `rr_tail` comienzan como NULL
2. Primer `rr_add_process()` crea cola inicial
3. Primer `rr_schedule_next()` realiza cambio de contexto inicial a modo usuario

## Ejemplo de Flujo de Planificación

```
1. Proceso A agregado → rr_add_process(proc_a) → Cola: [A]
2. Proceso B agregado → rr_add_process(proc_b) → Cola: [A] → [B]
3. Primera planificación → rr_schedule_next() → jmp_ring3(proc_a) → A se ejecuta
4. A llama syscall → Retorna al kernel → rr_schedule_next()
5. Cambio de contexto → switch_context_x64(A, B) → B se ejecuta
6. B llama syscall → Retorna al kernel → rr_schedule_next()
7. Cambio de contexto → switch_context_x64(B, A) → A se ejecuta (round-robin)
```

## Mejoras Futuras

Mejoras potenciales para el planificador:

1. **CFS (Completely Fair Scheduler)** - Ya implementado en `kernel/scheduler/` pero no usado
2. **Planificación Preventiva** - Interrupción de timer dispara cambio de contexto
3. **Prioridades de Proceso** - Planificación basada en prioridad
4. **Valores Nice** - Prioridad de proceso ajustable (-20 a +19)
5. **Soporte SMP** - Colas de planificador por CPU para sistemas multiprocesador
6. **Balanceo de Carga** - Distribuir procesos entre CPUs

## Manejo de Errores

El planificador usa aserciones BUG_ON para errores críticos:

- Sin memoria al asignar nodo del planificador
- Puntero de proceso inválido
- Nodo del planificador sin puntero de proceso

Todos los errores disparan panic del kernel para debugging inmediato.

