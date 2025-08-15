// kernel/scheduler/task.h -
#pragma once
#include <stdint.h>
#include <print.h>

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
    // ===============================================================================
    // CAMPOS BÁSICOS (compatibilidad con código existente)
    // ===============================================================================
    uint32_t pid;       // Process ID único
    uintptr_t esp;      // Stack Pointer - dónde está la pila del proceso
    uintptr_t ebp;      // Base Pointer - base del stack frame actual
    uintptr_t eip;      // Instruction Pointer - próxima instrucción a ejecutar
    uintptr_t cr3;      // Page Directory - espacio de memoria virtual del proceso
    uint8_t priority;   // Prioridad (0-255, mayor número = mayor prioridad)
    task_state_t state; // Estado actual del proceso
    struct task *next;  // Puntero al siguiente proceso (lista circular)

    // ===============================================================================
    // CAMPOS EXTENDIDOS PARA CFS
    // ===============================================================================
    uint64_t vruntime;    // Virtual runtime para CFS (nanosegundos virtuales)
    uint64_t exec_time;   // Tiempo real de ejecución acumulado
    uint64_t time_slice;  // Quantum asignado por CFS (nanosegundos)
    uint64_t slice_start; // Timestamp cuando inició el slice actual
    int8_t nice;          // Valor nice (-20 a +19, default 0)

    // ===============================================================================
    // CAMPOS PARA ÁRBOL ROJO-NEGRO (CFS)
    // ===============================================================================
    struct task *prev;      // Puntero a tarea anterior (listas doblemente enlazadas)
    struct task *rb_left;   // Hijo izquierdo en RB-tree
    struct task *rb_right;  // Hijo derecho en RB-tree
    struct task *rb_parent; // Padre en RB-tree
    int rb_color;           // Color del nodo (0=rojo, 1=negro)

    // ===============================================================================
    // CAMPOS ADICIONALES PARA GESTIÓN DE PROCESOS
    // ===============================================================================
    void *stack_base;      // Base del stack del proceso
    uint32_t stack_size;   // Tamaño del stack
    void (*entry)(void *); // Punto de entrada del proceso
    void *entry_arg;       // Argumento para la función de entrada

    // Estadísticas y debugging
    uint32_t context_switches; // Número de context switches
    uint64_t total_runtime;    // Tiempo total de CPU usado
    uint64_t last_run_time;    // Última vez que fue ejecutado

} task_t;



// ===============================================================================
// CONSTANTES Y LÍMITES
// ===============================================================================

#define MAX_TASKS 256                 // Máximo número de tareas
#define DEFAULT_STACK_SIZE (4 * 1024) // 4KB stack por defecto
#define MAX_NICE 19                   // Nice máximo
#define MIN_NICE -20                  // Nice mínimo
#define DEFAULT_NICE 0                // Nice por defecto


// ===============================================================================
// MACROS DE UTILIDAD
// ===============================================================================

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

// ===============================================================================
// FUNCIONES DE GESTIÓN DE TAREAS (declaraciones)
// ===============================================================================

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

// ===============================================================================
// FUNCIONES DE UTILIDAD
// ===============================================================================

// Obtener idle task
task_t *get_idle_task(void);

// Obtener lista de tareas
task_t *get_task_list(void);

// Obtener número de tareas activas
uint32_t get_task_count(void);

// ===============================================================================
// IMPLEMENTACIÓN DE FUNCIONES DE TESTING
// ===============================================================================

// Declaración de create_test_tasks
void create_test_tasks(void);