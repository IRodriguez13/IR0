# IR0 Kernel Process Management Subsystem

## Overview

The process management subsystem handles the complete lifecycle of processes in the IR0 kernel, including creation, execution, termination, and parent-child relationships. It provides process isolation through separate page directories and supports signal-based inter-process communication.

## Architecture

The process subsystem is located in `kernel/process.c` and `kernel/process.h`. It integrates with:
- **Memory Management** - Page directories for process isolation
- **Scheduler** - Process state management and context switching
- **Signals** - Process communication and exception handling
- **ELF Loader** - Binary loading for user programs

## Process Structure

Each process is represented by a `process_t` structure containing:

- **Task Context** (`task_t`) - CPU register state (RAX, RBX, RCX, RDX, RSI, RDI, RSP, RBP, RIP, RFLAGS, segments)
- **Process ID** (`pid_t`) - Unique process identifier (starts from PID 2, PID 1 is init)
- **Parent Process ID** (`ppid`) - Parent process identifier
- **Process State** - READY, RUNNING, BLOCKED, or ZOMBIE
- **Execution Mode** - KERNEL_MODE or USER_MODE
- **Page Directory** - Virtual memory isolation (separate page tables per process)
- **Memory Layout** - Stack, heap boundaries, and memory mappings
- **File Descriptors** - Table of open file descriptors (max 32 per process)
- **User Permissions** - UID, GID, EUID, EGID, umask
- **Current Working Directory** - Process working directory path
- **Command Name** - Process command name (for `ps` command)
- **Signal Pending** - Bitmask of pending signals

## Process States

Processes can be in one of four states:

- **PROCESS_READY** (0) - Process is ready to run, waiting to be scheduled
- **PROCESS_RUNNING** (1) - Process is currently executing on CPU
- **PROCESS_BLOCKED** (2) - Process is waiting for I/O or other events
- **PROCESS_ZOMBIE** (3) - Process has terminated but parent hasn't called `wait()` yet

## Process Lifecycle

### Process Creation

#### `process_spawn()`

Creates a new process deterministically with a specified entry point and name. This is the primary process creation method.

```c
pid_t process_spawn(void (*entry)(void), const char *name);
```

**Features:**
- Creates isolated process with separate page directory
- Allocates 8KB stack
- Sets up CPU context for user mode (Ring 3)
- Registers process in scheduler
- Assigns sequential PID (starting from 2)
- Sets parent to current process

**Memory Setup:**
- New page directory created via `create_process_page_directory()`
- Stack allocated via `kmalloc()` (8KB default)
- CPU registers initialized for user mode execution
- File descriptor table initialized

#### `process_fork()`

Traditional Unix `fork()` implementation (experimental). Creates a child process as a copy of the parent.

**Note:** This implementation does not use copy-on-write. The child shares the parent's memory space initially.

### Process Execution

Processes execute in user mode (Ring 3) with:
- **Code Segment** (CS) - 0x1B (user code segment)
- **Stack Segment** (SS) - 0x23 (user data segment)
- **Data Segments** (DS, ES, FS, GS) - 0x23 (user data segments)
- **RFLAGS** - 0x202 (interrupts enabled, IOPL=0)

### Process Termination

#### `process_exit()`

Terminates the current process with an exit code.

**Behavior:**
1. Sets process state to ZOMBIE
2. Stores exit code
3. Reparents orphaned children to init (PID 1)
4. Sends SIGCHLD signal to parent process
5. Halts execution until reaped by parent

**Zombie Handling:**
- Zombie processes remain in the process list until parent calls `wait()`
- If parent dies, children are reparented to init (PID 1)
- Init process can automatically reap zombie children

### Process Waiting

#### `process_wait()`

Waits for a child process to terminate and retrieves its exit status.

```c
int process_wait(pid_t pid, int *status);
```

**Behavior:**
1. Searches for zombie child process with matching PID
2. If found, retrieves exit code and removes from process list
3. If not found, yields CPU and continues waiting
4. Returns child PID on success, or continues waiting

**Orphan Handling:**
- If parent process exits before calling `wait()`, children become orphans
- Orphaned processes are automatically reparented to init (PID 1)
- Init process can call `process_reap_zombies()` to clean up zombie children

## Signal System

The process subsystem integrates with the signal system for inter-process communication and error handling.

### Signal Types

**Hardware/CPU Exceptions:**
- `SIGSEGV` (11) - Segmentation violation (invalid memory access)
- `SIGFPE` (8) - Floating point exception (divide by zero)
- `SIGILL` (4) - Illegal instruction
- `SIGBUS` (7) - Bus error (misaligned memory access)
- `SIGTRAP` (5) - Trace/breakpoint trap

**Termination Signals:**
- `SIGKILL` (9) - Kill process (cannot be caught)
- `SIGTERM` (15) - Termination signal
- `SIGINT` (2) - Interrupt (Ctrl+C)
- `SIGQUIT` (3) - Quit (Ctrl+\)

**Process Control:**
- `SIGCHLD` (17) - Child process terminated or stopped
- `SIGSTOP` (19) - Stop process (cannot be caught)
- `SIGCONT` (18) - Continue if stopped

### Signal Handling

Signals are handled before each context switch:
1. Scheduler calls `handle_signals()` before switching processes
2. Signals are checked in priority order (SIGKILL, CPU exceptions, termination signals)
3. Fatal signals (SIGKILL, SIGSEGV, SIGFPE) terminate the process
4. SIGCHLD notifies parent when child terminates
5. Signal bitmask is cleared after handling

### CPU Exception Mapping

CPU exceptions (0-31) are automatically mapped to signals:
- Exception 0 (Divide by Zero) → SIGFPE
- Exception 6 (Invalid Opcode) → SIGILL
- Exception 8 (Double Fault) → SIGABRT
- Exception 13 (General Protection Fault) → SIGSEGV
- Exception 14 (Page Fault) → SIGSEGV

## Process Memory Isolation

Each process has its own page directory (CR3 register) for memory isolation:

- **Kernel Space** - Shared across all processes (identity mapped)
- **User Space** - Process-specific virtual address space
- **Stack** - Private 8KB stack per process
- **Heap** - Managed by `brk()` syscall (per-process heap)

## Process Permissions

Processes maintain Unix-like permissions:

- **UID/GID** - User and group IDs (default: root/0)
- **EUID/EGID** - Effective user and group IDs
- **umask** - Default file creation mask (default: 0022)

## File Descriptors

Each process has a file descriptor table (max 32 FDs):

- **Standard Input** (FD 0) - Usually `/dev/console`
- **Standard Output** (FD 1) - Usually `/dev/console`
- **Standard Error** (FD 2) - Usually `/dev/console`

File descriptors are initialized in `process_init_fd_table()`.

## Process Query Functions

- `process_get_pid()` - Get current process PID
- `process_get_ppid()` - Get parent process PID
- `process_get_current()` - Get current process pointer
- `get_process_list()` - Get all processes
- `process_find_by_pid(pid)` - Find process by PID

## Initialization

The process subsystem is initialized in `process_init()`:

1. Initializes process list (empty)
2. Sets next PID to 2 (PID 1 is init)
3. Initializes simple user system

## Special Processes

### Init Process (PID 1)

The init process is the first user-space process:

- Created at kernel boot
- All orphaned processes are reparented to init
- Can automatically reap zombie children via `process_reap_zombies()`
- Typically runs a shell or service manager

### Kernel Mode Processes

Some processes run in kernel mode (KERNEL_MODE):

- **Debug Shell** - Interactive kernel debugger
- **Embedded Init** - Kernel-embedded init process (if not loading from disk)

Kernel mode processes bypass user address validation in `copy_from_user()`/`copy_to_user()`.

## Error Handling

Process operations return standard error codes:

- `0` - Success
- `-1` - Generic error
- `-ENOMEM` - Out of memory
- `-ESRCH` - Process not found

All errors are logged to serial console for debugging.

## Implementation Notes

- Process list is a simple linked list (not optimized for large numbers of processes)
- No process priorities or nice values in current scheduler
- No threads - each process is a single thread
- Memory is not copy-on-write for fork (intentional limitation)
- Signal handling is synchronous (handled before context switch)

## Process Lifecycle Example

```
1. Kernel boot → process_init()
2. Create init (PID 1) → process_spawn(init_entry, "init")
3. Init spawns shell → process_spawn(shell_entry, "shell")
4. Shell runs user program → process_spawn(program_entry, "program")
5. Program exits → process_exit(0) → SIGCHLD to parent
6. Shell calls wait() → process_wait(child_pid, &status)
7. Process removed from list → kfree(process)
```

---

# Subsistema de Gestión de Procesos del Kernel IR0

## Resumen

El subsistema de gestión de procesos maneja el ciclo de vida completo de los procesos en el kernel IR0, incluyendo creación, ejecución, terminación y relaciones padre-hijo. Proporciona aislamiento de procesos a través de directorios de página separados y soporta comunicación entre procesos basada en señales.

## Arquitectura

El subsistema de procesos está ubicado en `kernel/process.c` y `kernel/process.h`. Se integra con:
- **Gestión de Memoria** - Directorios de página para aislamiento de procesos
- **Planificador** - Gestión de estado de procesos y cambio de contexto
- **Señales** - Comunicación entre procesos y manejo de excepciones
- **Cargador ELF** - Carga de binarios para programas de usuario

## Estructura de Proceso

Cada proceso está representado por una estructura `process_t` que contiene:

- **Contexto de Tarea** (`task_t`) - Estado de registros CPU (RAX, RBX, RCX, RDX, RSI, RDI, RSP, RBP, RIP, RFLAGS, segmentos)
- **ID de Proceso** (`pid_t`) - Identificador único de proceso (comienza desde PID 2, PID 1 es init)
- **ID de Proceso Padre** (`ppid`) - Identificador del proceso padre
- **Estado del Proceso** - READY, RUNNING, BLOCKED, o ZOMBIE
- **Modo de Ejecución** - KERNEL_MODE o USER_MODE
- **Directorio de Página** - Aislamiento de memoria virtual (tablas de página separadas por proceso)
- **Layout de Memoria** - Límites de stack y heap, y mapeos de memoria
- **Descriptores de Archivo** - Tabla de descriptores de archivo abiertos (máx. 32 por proceso)
- **Permisos de Usuario** - UID, GID, EUID, EGID, umask
- **Directorio de Trabajo Actual** - Ruta del directorio de trabajo del proceso
- **Nombre de Comando** - Nombre del comando del proceso (para comando `ps`)
- **Señal Pendiente** - Máscara de bits de señales pendientes

## Estados de Proceso

Los procesos pueden estar en uno de cuatro estados:

- **PROCESS_READY** (0) - Proceso está listo para ejecutar, esperando ser planificado
- **PROCESS_RUNNING** (1) - Proceso está ejecutándose actualmente en CPU
- **PROCESS_BLOCKED** (2) - Proceso está esperando I/O u otros eventos
- **PROCESS_ZOMBIE** (3) - Proceso ha terminado pero el padre no ha llamado `wait()` todavía

## Ciclo de Vida del Proceso

### Creación de Proceso

#### `process_spawn()`

Crea un nuevo proceso determinísticamente con un punto de entrada y nombre especificados. Este es el método principal de creación de procesos.

```c
pid_t process_spawn(void (*entry)(void), const char *name);
```

**Características:**
- Crea proceso aislado con directorio de página separado
- Asigna stack de 8KB
- Configura contexto CPU para modo usuario (Ring 3)
- Registra proceso en planificador
- Asigna PID secuencial (comenzando desde 2)
- Establece padre al proceso actual

**Configuración de Memoria:**
- Nuevo directorio de página creado via `create_process_page_directory()`
- Stack asignado via `kmalloc()` (8KB por defecto)
- Registros CPU inicializados para ejecución en modo usuario
- Tabla de descriptores de archivo inicializada

#### `process_fork()`

Implementación tradicional de `fork()` de Unix (experimental). Crea un proceso hijo como copia del padre.

**Nota:** Esta implementación no usa copy-on-write. El hijo comparte inicialmente el espacio de memoria del padre.

### Ejecución de Proceso

Los procesos se ejecutan en modo usuario (Ring 3) con:
- **Segmento de Código** (CS) - 0x1B (segmento de código de usuario)
- **Segmento de Stack** (SS) - 0x23 (segmento de datos de usuario)
- **Segmentos de Datos** (DS, ES, FS, GS) - 0x23 (segmentos de datos de usuario)
- **RFLAGS** - 0x202 (interrupciones habilitadas, IOPL=0)

### Terminación de Proceso

#### `process_exit()`

Termina el proceso actual con un código de salida.

**Comportamiento:**
1. Establece estado del proceso a ZOMBIE
2. Almacena código de salida
3. Reasigna procesos hijos huérfanos a init (PID 1)
4. Envía señal SIGCHLD al proceso padre
5. Detiene ejecución hasta ser limpiado por el padre

**Manejo de Zombies:**
- Los procesos zombie permanecen en la lista de procesos hasta que el padre llama `wait()`
- Si el padre muere, los hijos son reasignados a init (PID 1)
- El proceso init puede limpiar automáticamente procesos hijos zombie

### Espera de Proceso

#### `process_wait()`

Espera a que un proceso hijo termine y recupera su estado de salida.

```c
int process_wait(pid_t pid, int *status);
```

**Comportamiento:**
1. Busca proceso hijo zombie con PID coincidente
2. Si se encuentra, recupera código de salida y elimina de la lista de procesos
3. Si no se encuentra, cede CPU y continúa esperando
4. Devuelve PID del hijo en éxito, o continúa esperando

**Manejo de Huérfanos:**
- Si el proceso padre termina antes de llamar `wait()`, los hijos se vuelven huérfanos
- Los procesos huérfanos son automáticamente reasignados a init (PID 1)
- El proceso init puede llamar `process_reap_zombies()` para limpiar procesos hijo zombie

## Sistema de Señales

El subsistema de procesos se integra con el sistema de señales para comunicación entre procesos y manejo de errores.

### Tipos de Señales

**Excepciones de Hardware/CPU:**
- `SIGSEGV` (11) - Violación de segmentación (acceso de memoria inválido)
- `SIGFPE` (8) - Excepción de punto flotante (división por cero)
- `SIGILL` (4) - Instrucción ilegal
- `SIGBUS` (7) - Error de bus (acceso de memoria desalineado)
- `SIGTRAP` (5) - Trampa de rastreo/punto de interrupción

**Señales de Terminación:**
- `SIGKILL` (9) - Matar proceso (no puede ser capturada)
- `SIGTERM` (15) - Señal de terminación
- `SIGINT` (2) - Interrupción (Ctrl+C)
- `SIGQUIT` (3) - Salir (Ctrl+\)

**Control de Proceso:**
- `SIGCHLD` (17) - Proceso hijo terminado o detenido
- `SIGSTOP` (19) - Detener proceso (no puede ser capturada)
- `SIGCONT` (18) - Continuar si está detenido

### Manejo de Señales

Las señales se manejan antes de cada cambio de contexto:
1. El planificador llama `handle_signals()` antes de cambiar procesos
2. Las señales se verifican en orden de prioridad (SIGKILL, excepciones CPU, señales de terminación)
3. Señales fatales (SIGKILL, SIGSEGV, SIGFPE) terminan el proceso
4. SIGCHLD notifica al padre cuando el hijo termina
5. La máscara de bits de señales se limpia después del manejo

### Mapeo de Excepciones CPU

Las excepciones CPU (0-31) se mapean automáticamente a señales:
- Excepción 0 (División por Cero) → SIGFPE
- Excepción 6 (Opcode Inválido) → SIGILL
- Excepción 8 (Doble Fallo) → SIGABRT
- Excepción 13 (Fallo de Protección General) → SIGSEGV
- Excepción 14 (Fallo de Página) → SIGSEGV

## Aislamiento de Memoria de Proceso

Cada proceso tiene su propio directorio de página (registro CR3) para aislamiento de memoria:

- **Espacio del Kernel** - Compartido entre todos los procesos (mapeado idéntico)
- **Espacio de Usuario** - Espacio de direcciones virtuales específico del proceso
- **Stack** - Stack privado de 8KB por proceso
- **Heap** - Gestionado por syscall `brk()` (heap por proceso)

## Permisos de Proceso

Los procesos mantienen permisos estilo Unix:

- **UID/GID** - IDs de usuario y grupo (por defecto: root/0)
- **EUID/EGID** - IDs efectivos de usuario y grupo
- **umask** - Máscara de creación de archivos por defecto (por defecto: 0022)

## Descriptores de Archivo

Cada proceso tiene una tabla de descriptores de archivo (máx. 32 FDs):

- **Entrada Estándar** (FD 0) - Usualmente `/dev/console`
- **Salida Estándar** (FD 1) - Usualmente `/dev/console`
- **Error Estándar** (FD 2) - Usualmente `/dev/console`

Los descriptores de archivo se inicializan en `process_init_fd_table()`.

## Funciones de Consulta de Proceso

- `process_get_pid()` - Obtener PID del proceso actual
- `process_get_ppid()` - Obtener PID del proceso padre
- `process_get_current()` - Obtener puntero al proceso actual
- `get_process_list()` - Obtener todos los procesos
- `process_find_by_pid(pid)` - Buscar proceso por PID

## Inicialización

El subsistema de procesos se inicializa en `process_init()`:

1. Inicializa lista de procesos (vacía)
2. Establece siguiente PID a 2 (PID 1 es init)
3. Inicializa sistema simple de usuarios

## Procesos Especiales

### Proceso Init (PID 1)

El proceso init es el primer proceso del espacio de usuario:

- Creado al arrancar el kernel
- Todos los procesos huérfanos son reasignados a init
- Puede limpiar automáticamente procesos hijo zombie via `process_reap_zombies()`
- Típicamente ejecuta un shell o gestor de servicios

### Procesos en Modo Kernel

Algunos procesos se ejecutan en modo kernel (KERNEL_MODE):

- **Shell de Debug** - Depurador interactivo del kernel
- **Init Embebido** - Proceso init embebido en kernel (si no se carga desde disco)

Los procesos en modo kernel omiten la validación de direcciones de usuario en `copy_from_user()`/`copy_to_user()`.

## Manejo de Errores

Las operaciones de proceso devuelven códigos de error estándar:

- `0` - Éxito
- `-1` - Error genérico
- `-ENOMEM` - Sin memoria
- `-ESRCH` - Proceso no encontrado

Todos los errores se registran en la consola serial para debugging.

## Notas de Implementación

- La lista de procesos es una lista enlazada simple (no optimizada para grandes números de procesos)
- No hay prioridades de proceso o valores nice en el planificador actual
- No hay hilos - cada proceso es un solo hilo
- La memoria no es copy-on-write para fork (limitación intencional)
- El manejo de señales es síncrono (manejado antes del cambio de contexto)

## Ejemplo de Ciclo de Vida de Proceso

```
1. Arranque del kernel → process_init()
2. Crear init (PID 1) → process_spawn(init_entry, "init")
3. Init genera shell → process_spawn(shell_entry, "shell")
4. Shell ejecuta programa de usuario → process_spawn(program_entry, "program")
5. Programa termina → process_exit(0) → SIGCHLD al padre
6. Shell llama wait() → process_wait(child_pid, &status)
7. Proceso eliminado de lista → kfree(process)
```

