// kernel/process/process.h - Sistema de gestión de procesos
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Tipos básicos
typedef int32_t pid_t;

// ===============================================================================
// CONSTANTES Y DEFINICIONES
// ===============================================================================

#define MAX_PROCESSES 1024
#define MAX_PROCESS_NAME 64
#define MAX_PROCESS_ARGS 16
#define MAX_PROCESS_ENV 32

// Estados de proceso
typedef enum
{
    PROCESS_NEW = 0,  // Proceso recién creado
    PROCESS_READY,    // Listo para ejecutar
    PROCESS_RUNNING,  // Actualmente ejecutándose
    PROCESS_SLEEPING, // Durmiendo (waiting)
    PROCESS_STOPPED,  // Detenido (SIGSTOP)
    PROCESS_ZOMBIE,   // Terminado pero no reaped
    PROCESS_DEAD      // Completamente terminado
} process_state_t;

// Prioridades de proceso
typedef enum
{
    PRIORITY_IDLE = 0,    // Solo cuando no hay nada más
    PRIORITY_LOW = 1,     // Procesos de baja prioridad
    PRIORITY_NORMAL = 2,  // Prioridad normal (default)
    PRIORITY_HIGH = 3,    // Procesos de alta prioridad
    PRIORITY_REALTIME = 4 // Tiempo real
} process_priority_t;

// Flags de proceso
#define PROCESS_FLAG_KERNEL (1 << 0)   // Proceso kernel
#define PROCESS_FLAG_USER (1 << 1)     // Proceso usuario
#define PROCESS_FLAG_DAEMON (1 << 2)   // Proceso daemon
#define PROCESS_FLAG_ORPHAN (1 << 3)   // Proceso huérfano
#define PROCESS_FLAG_SIGNALED (1 << 4) // Recibió señal
#define PROCESS_FLAG_TRACED (1 << 5)   // En debug

// ===============================================================================
// ESTRUCTURA DE PROCESO MEJORADA
// ===============================================================================

typedef struct process
{
    // Identificación
    pid_t pid;                   // Process ID único
    pid_t ppid;                  // Parent Process ID
    pid_t pgid;                  // Process Group ID
    char name[MAX_PROCESS_NAME]; // Nombre del proceso

    // Estado y control
    process_state_t state;       // Estado actual
    process_priority_t priority; // Prioridad de scheduling
    uint32_t flags;              // Flags de proceso
    int exit_code;               // Código de salida

    // Scheduling
    uint64_t cpu_time;   // Tiempo total de CPU usado
    uint64_t start_time; // Timestamp de creación
    uint64_t last_run;   // Última vez que ejecutó
    uint32_t time_slice; // Time slice actual

    // Memoria
    uintptr_t page_directory; // Directorio de páginas (CR3)
    uintptr_t kernel_stack;   // Stack del kernel
    uintptr_t user_stack;     // Stack del usuario
    uintptr_t heap_start;     // Inicio del heap
    uintptr_t heap_end;       // Fin del heap

    // Contexto de ejecución
    struct
    {
        // Registros generales
        uint64_t rax, rbx, rcx, rdx;
        uint64_t rsi, rdi, rbp, rsp;
        uint64_t r8, r9, r10, r11, r12, r13, r14, r15;

        // Registros de segmento
        uint64_t cs, ds, es, fs, gs, ss;

        // Registros de control
        uint64_t rip;    // Instruction Pointer
        uint64_t rflags; // Flags register

        // FPU/SSE state (opcional)
        uint8_t fpu_state[512]; // Estado FPU/SSE
        bool fpu_used;          // Si usó FPU/SSE
    } context;

    // Archivos y recursos
    int open_files[16];    // File descriptors abiertos
    uintptr_t working_dir; // Directorio de trabajo (inode)
    
    // User/Group management
    uint32_t uid;          // User ID
    uint32_t gid;          // Group ID
    
    // Memory management
    uintptr_t heap_break;  // Current heap break
    uintptr_t next_mmap_addr; // Next mmap address

    // Señales
    uint32_t signal_mask;     // Máscara de señales bloqueadas
    uint32_t pending_signals; // Señales pendientes

    // Argumentos y entorno
    char *argv[MAX_PROCESS_ARGS]; // Argumentos del programa
    char *envp[MAX_PROCESS_ENV];  // Variables de entorno
    int argc;                     // Número de argumentos
    int envc;                     // Número de variables de entorno

    // Lista enlazada
    struct process *next;     // Siguiente proceso en lista
    struct process *prev;     // Proceso anterior en lista
    struct process *children; // Lista de procesos hijos
    struct process *sibling;  // Siguiente hermano

} process_t;

// ===============================================================================
// FUNCIONES DE GESTIÓN DE PROCESOS
// ===============================================================================

// Inicialización
void process_init(void);
process_t *process_create(const char *name, void (*entry_point)(void *), void *arg);
void process_destroy(process_t *process);

// Control de procesos
int process_fork(process_t *parent);
int process_exec(const char *path, char *const argv[], char *const envp[]);
void process_exit(int exit_code);
int process_wait(pid_t pid, int *status);

// Scheduling
void process_schedule(void);
void process_yield(void);
void process_sleep(uint32_t ms);
void process_wakeup(process_t *process);

// Context switching
void process_switch(process_t *from, process_t *to);
void process_save_context(process_t *process);
void process_restore_context(process_t *process);

// Búsqueda y gestión
process_t *process_find_by_pid(pid_t pid);
process_t *process_get_current(void);
pid_t process_get_pid(void);
pid_t process_get_ppid(void);

// Señales
void process_send_signal(pid_t pid, int signal);
void process_handle_signals(process_t *process);

// Debug y estadísticas
void process_print_info(process_t *process);
void process_print_all(void);
uint32_t process_get_count(void);

// ===============================================================================
// VARIABLES GLOBALES
// ===============================================================================

extern process_t *current_process;
extern process_t *idle_process;
extern uint32_t process_count;
extern pid_t next_pid;

// ===============================================================================
// MACROS ÚTILES
// ===============================================================================

#define CURRENT_PROCESS() (process_get_current())
#define CURRENT_PID() (process_get_pid())
#define IS_KERNEL_PROCESS(p) ((p)->flags & PROCESS_FLAG_KERNEL)
#define IS_USER_PROCESS(p) ((p)->flags & PROCESS_FLAG_USER)
