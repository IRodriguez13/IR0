// kernel/scheduler/task.h -
#pragma once
#include <stdint.h>
#include <ir0/print.h>

typedef enum
{
    TASK_READY,     // 0, listo para ejecutar
    TASK_RUNNING,   // 1 en ejecución
    TASK_BLOCKED,   // 2 esperando IO, mutex, etc
    TASK_TERMINATED // proceso terminado
} task_state_t;

/*
Estructura de proceso extendida para soportar múltiples schedulers
Todos los registros que manejo en 32/64 bits
*/

typedef struct task
{
    uint64_t rax;      // +0x00: RAX register
    uint64_t rbx;      // +0x08: RBX register
    uint64_t rcx;      // +0x10: RCX register
    uint64_t rdx;      // +0x18: RDX register
    uint64_t rsi;      // +0x20: RSI register
    uint64_t rdi;      // +0x28: RDI register
    uint64_t r8;       // +0x30: R8 register
    uint64_t r9;       // +0x38: R9 register
    uint64_t r10;      // +0x40: R10 register
    uint64_t r11;      // +0x48: R11 register
    uint64_t r12;      // +0x50: R12 register
    uint64_t r13;      // +0x58: R13 register
    uint64_t r14;      // +0x60: R14 register
    uint64_t r15;      // +0x68: R15 register
    uint64_t rsp;      // +0x70: Stack Pointer
    uint64_t rbp;      // +0x78: Base Pointer
    uint64_t rip;      // +0x80: Instruction Pointer
    uint64_t rflags;   // +0x88: Flags register
    uint16_t cs;       // +0x90: Code Segment
    uint16_t ds;       // +0x92: Data Segment
    uint16_t es;       // +0x94: Extra Segment
    uint16_t fs;       // +0x96: FS Segment
    uint16_t gs;       // +0x98: GS Segment
    uint16_t ss;       // +0x9A: Stack Segment
    uint16_t padding1; // +0x9C: Padding
    uint32_t padding2; // +0x9E: Padding
    uint64_t cr0;      // +0xA0: Control Register 0
    uint64_t cr2;      // +0xA8: Control Register 2
    uint64_t cr3;      // +0xB0: Control Register 3 (Page Directory)
    uint64_t cr4;      // +0xB8: Control Register 4
    uint64_t dr0;      // +0xC0: Debug Register 0
    uint64_t dr1;      // +0xC8: Debug Register 1
    uint64_t dr2;      // +0xD0: Debug Register 2
    uint64_t dr3;      // +0xD8: Debug Register 3
    uint64_t dr6;      // +0xE0: Debug Register 6
    uint64_t dr7;      // +0xE8: Debug Register 7

    uint32_t pid;       // Process ID único
    uint8_t priority;   // Prioridad (0-255, mayor número = mayor prioridad)
    task_state_t state; // Estado actual del proceso
    struct task *next;  // Puntero al siguiente proceso (lista circular)

    uint64_t vruntime;    // Virtual runtime para CFS (nanosegundos virtuales)
    uint64_t exec_time;   // Tiempo real de ejecución acumulado
    uint64_t time_slice;  // Quantum asignado por CFS (nanosegundos)
    uint64_t slice_start; // Timestamp cuando inició el slice actual
    int8_t nice;          // Valor nice (-20 a +19, default 0)

    struct task *prev;      // Puntero a tarea anterior (listas doblemente enlazadas)
    struct task *rb_left;   // Hijo izquierdo en RB-tree
    struct task *rb_right;  // Hijo derecho en RB-tree
    struct task *rb_parent; // Padre en RB-tree
    int rb_color;           // Color del nodo (0=rojo, 1=negro)

    void *stack_base;      // Base del stack del proceso
    uint32_t stack_size;   // Tamaño del stack
    void (*entry)(void *); // Punto de entrada del proceso
    void *entry_arg;       // Argumento para la función de entrada

    // Estadísticas y debugging
    uint32_t context_switches; // Número de context switches
    uint64_t total_runtime;    // Tiempo total de CPU usado
    uint64_t last_run_time;    // Última vez que fue ejecutado

} task_t;



#define MAX_TASKS 256                 // Máximo número de tareas
#define DEFAULT_STACK_SIZE (4 * 1024) // 4KB stack por defecto
#define MAX_NICE 19                   // Nice máximo
#define MIN_NICE -20                  // Nice mínimo
#define DEFAULT_NICE 0                // Nice por defecto



#define TASK_INIT(name, prio, nice_val) { \
    .pid = 0,                             \
    .priority = (prio),                   \
    .state = TASK_READY,                  \
    .nice = (nice_val),                   \
    .vruntime = 0,                        \
    .exec_time = 0,                       \
    .time_slice = 0,                      \
    .context_switches = 0,                \
    .total_runtime = 0,                   \
    .next = NULL,                         \
    .prev = NULL,                         \
    .rb_left = NULL,                      \
    .rb_right = NULL,                     \
    .rb_parent = NULL,                    \
    .rb_color = RB_RED}

// Macros para verificar estado de tarea
#define task_is_ready(t) ((t)->state == TASK_READY)
#define task_is_running(t) ((t)->state == TASK_RUNNING)
#define task_is_blocked(t) ((t)->state == TASK_BLOCKED)
#define task_is_terminated(t) ((t)->state == TASK_TERMINATED)


// Crear nueva tarea
task_t *create_task(void (*entry)(void *), void *arg, uint8_t priority, int8_t nice);

// Destruir tarea
void destroy_task(task_t *task);

// Cambiar nice de una tarea
void task_set_nice(task_t *task, int8_t nice);

// Obtener información de la tarea
void task_get_info(task_t *task);

// Funciones para testing
void create_test_tasks(void);

// Variables globales importantes
extern task_t *current_running_task; // Tarea actualmente ejecutándose


// Obtener idle task
task_t *get_idle_task(void);

// Obtener lista de tareas
task_t *get_task_list(void);

// Obtener número de tareas activas
uint32_t get_task_count(void);


// Declaración de create_test_tasks
void create_test_tasks(void);