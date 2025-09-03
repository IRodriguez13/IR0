// ===============================================================================
//  round-robin_scheduler.c -
// ===============================================================================

// kernel/scheduler/round-robin_scheduler.c - VERSIÓN CORREGIDA
#include <print.h>
#include <panic/panic.h>
#include <stddef.h>
#include "scheduler_types.h"
#include "task.h"
#include "scheduler.h"

// Estructura privada del round-robin scheduler
typedef struct
{
    task_t *ready_queue;    // Cola circular de procesos listos
    task_t *current_task;   // Proceso actualmente ejecutando
    uint32_t task_count;    // Número de tareas en la cola
    uint32_t time_slice;    // Quantum de tiempo (en ticks)
    uint32_t current_ticks; // Ticks transcurridos del proceso actual
} roundrobin_state_t;

static roundrobin_state_t rr_state;
extern void switch_task(task_t *current, task_t *next);

static void rr_init(void)
{
    LOG_OK("Initializing Round-Robin scheduler");
    rr_state.ready_queue = NULL;
    rr_state.current_task = NULL;
    rr_state.task_count = 0;
    rr_state.time_slice = 5; // 5 ticks por defecto
    rr_state.current_ticks = 0;
}

static void rr_add_task(task_t *task)
{
    if (!task)
    {
        LOG_ERR("rr_add_task: task is NULL");
        return;
    }

    if (task->state == TASK_TERMINATED)
    {
        LOG_WARN("rr_add_task: trying to add terminated task");
        return;
    }

    task->state = TASK_READY;

    if (!rr_state.ready_queue)
    {
        // Primera tarea: crear lista circular de 1 elemento
        rr_state.ready_queue = task;
        task->next = task;
        LOG_OK("Primera tarea agregada al RR scheduler");
    }
    else
    {
        // Buscar el último nodo de la lista circular
        task_t *last = rr_state.ready_queue;

        while (last->next != rr_state.ready_queue)
        {
            last = last->next;
            // Protección contra listas corruptas
            if (!last || !last->next)
            {
                LOG_ERR("Corrupted ready queue detected!");
                panic("RR Scheduler corruption detected");
                return;
            }
        }

        // Insertar nueva tarea al final
        last->next = task;
        task->next = rr_state.ready_queue;
        LOG_OK("Nueva tarea agregada al RR scheduler");
    }

    rr_state.task_count++;
}

static task_t *rr_pick_next_task(void)
{
    if (!rr_state.ready_queue)
    {
        return NULL; // No hay tareas
    }

    // Buscar próximo proceso READY
    task_t *next_task = rr_state.ready_queue;
    int attempts = 0;

    while (next_task->state != TASK_READY && attempts < MAX_TASKS)
    {
        next_task = next_task->next;
        attempts++;

        if (next_task == rr_state.ready_queue)
        {
            break; // Dimos la vuelta completa
        }
    }

    if (next_task->state != TASK_READY)
    {
        return NULL; // No hay tareas ready
    }

    // Actualizar ready_queue para apuntar al siguiente
    rr_state.ready_queue = next_task->next;

    return next_task;
}

static void rr_task_tick(void)
{
    if (!rr_state.current_task)
    {
        return;
    }

    rr_state.current_ticks++;

    // Verificar si se agotó el time slice
    if (rr_state.current_ticks >= rr_state.time_slice)
    {
        rr_state.current_ticks = 0;

        // Forzar context switch en próximo scheduler_tick
        // (esto se maneja en el scheduler central)
    }
}

static void rr_cleanup(void)
{
    LOG_OK("Round-Robin scheduler cleanup");
    rr_state.ready_queue = NULL;
    rr_state.current_task = NULL;
    rr_state.task_count = 0;
}

// ===============================================================================
// Funciones auxiliares para compatibilidad
// ===============================================================================

// Function moved to sched_central.c to avoid duplication

// ===============================================================================
// EXPORTAR LA ESTRUCTURA scheduler_ops_t - ESTO ES LO QUE FALTABA
// ===============================================================================

scheduler_ops_t roundrobin_scheduler_ops = 
{
    .type = SCHEDULER_ROUND_ROBIN,
    .name = "Round-Robin Scheduler",
    .init = rr_init,
    .add_task = rr_add_task,
    .pick_next_task = rr_pick_next_task,
    .task_tick = rr_task_tick,
    .cleanup = rr_cleanup,
    .private_data = &rr_state
};