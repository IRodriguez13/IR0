// ===============================================================================
//  round-robin_scheduler.c -
// ===============================================================================

// kernel/scheduler/round-robin_scheduler.c - VERSIÓN CORREGIDA
#include <print.h>
#include <panic/panic.h>
#include <stddef.h>
#include <stdbool.h>
#include "scheduler_types.h"
#include "task.h"
#include "scheduler.h"
#define SCHEDULER_CONTEXT_SWITCH (1 << 0)
unsigned int scheduler_flags = 0;

// Architecture-aware interrupt protection
#ifdef __x86_64__
static inline uint32_t interrupt_save_and_disable(void)
{
    uint64_t flags;
    __asm__ volatile("pushfq; cli; popq %0" : "=r"(flags)::"memory");
    return (uint32_t)flags;
}

static inline void interrupt_restore(uint32_t flags)
{
    if (flags & 0x200)
    { // IF flag was set
        __asm__ volatile("sti" ::: "memory");
    }
}
#else
static inline uint32_t interrupt_save_and_disable(void)
{
    uint32_t flags;
    __asm__ volatile("pushfl; cli; popl %0" : "=r"(flags)::"memory");
    return flags;
}

static inline void interrupt_restore(uint32_t flags)
{
    if (flags & 0x200)
    { // IF flag was set
        __asm__ volatile("sti" ::: "memory");
    }
}
#endif

// Debug logging macro
#ifndef LOG_DEBUG
#define LOG_DEBUG(fmt, ...) print("DEBUG: " fmt "\n")
#endif

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

static bool validate_circular_list(task_t *head)
{
    if (!head)
        return true; // Lista vacía es válida

    task_t *slow = head;
    task_t *fast = head;
    int count = 0;

    while (slow != fast && fast != head && fast->next != head)
        ;
    {
        if (!slow || !fast || !fast->next)
        {
            return false; // NULL pointer encontrado
        }

        slow = slow->next;
        fast = fast->next->next;
        count++;

        if (count > MAX_TASKS)
        {
            return false; // Lista demasiado larga
        }
    }

    return slow == fast || fast == head || fast->next == head;
}

static void remove_task_from_queue(task_t *task_to_remove)
{
    if (!task_to_remove || !rr_state.ready_queue)
    {
        return;
    }

    // Si es la única tarea
    if (rr_state.ready_queue->next == rr_state.ready_queue)
    {
        if (rr_state.ready_queue == task_to_remove)
        {
            rr_state.ready_queue = NULL;
        }
        
        return;
    }

    // Buscar el nodo anterior al que queremos remover
    task_t *current = rr_state.ready_queue;
    task_t *prev = NULL;

    while (current != rr_state.ready_queue)
    {
        if (current->next == task_to_remove)
        {
            prev = current;
            break;
        }
        current = current->next;
    }

    if (prev)
    {
        prev->next = task_to_remove->next;

        // Si removemos el head, actualizar ready_queue
        if (rr_state.ready_queue == task_to_remove)
        {
            rr_state.ready_queue = task_to_remove->next;
        }
    }
}

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
        print("RR: WARNING - trying to add terminated task PID ");
        print_hex_compact(task->pid);
        print("\n");
        return;
    }

    // CRITICAL: Disable interrupts for atomic operation
    uint32_t flags = interrupt_save_and_disable();

    task->state = TASK_READY;

    if (!rr_state.ready_queue)
    {
        // Primera tarea: crear lista circular de 1 elemento
        rr_state.ready_queue = task;
        task->next = task;
        rr_state.task_count = 1;
        print("RR: First task PID ");
        print_hex_compact(task->pid);
        print(" added to queue\n");
    }
    else
    {
        // Validar integridad de la lista circular ANTES de modificar
        if (!validate_circular_list(rr_state.ready_queue))
        {
            interrupt_restore(flags);
            panic("RR: Corrupted ready queue detected in add_task!");
        }

        // Buscar el último nodo de la lista circular
        task_t *last = rr_state.ready_queue;
        int safety_counter = 0;

        while (last->next != rr_state.ready_queue && safety_counter < MAX_TASKS)
        {
            last = last->next;
            safety_counter++;

            if (!last || !last->next)
            {
                interrupt_restore(flags);
                LOG_ERR("RR: NULL pointer in ready queue at position ");
                print_hex_compact(safety_counter);
                print("\n");
                panic("RR: Ready queue corruption detected!");
            }
        }

        if (safety_counter >= MAX_TASKS)
        {
            interrupt_restore(flags);
            LOG_ERR("RR: Infinite loop detected in ready queue");
            panic("RR: Ready queue infinite loop!");
        }

        // Insertar nueva tarea al final
        last->next = task;
        task->next = rr_state.ready_queue;
        rr_state.task_count++;

        print("RR: Task PID ");
        print_hex_compact(task->pid);
        print(" added to queue (total: ");
        print_hex_compact(rr_state.task_count);
        print(" tasks)\n");
    }

    interrupt_restore(flags);
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

    // itero si las tareas no son ready y no me paso de MAX_TASKS
    while (next_task->state != TASK_READY && attempts < MAX_TASKS)
    {
        // Avanzar al siguiente y aumentar el contador de intentos
        next_task = next_task->next;
        attempts++;

        // Si llego al inicio de la cola, terminar
        if (next_task == rr_state.ready_queue)
        {
            break; // Dimos la vuelta completa
        }
    }

    // Si la tarea encontrada es la actual, retorno NULL
    if (next_task == rr_state.current_task)
    {
        return NULL;
    }

    // Si llego al final de la cola sin encontrar una tarea ready
    if (next_task->state != TASK_READY)
    {
        return NULL; // No hay tareas ready
    }

    // Verificar si la tarea encontrada es NULL
    if (!next_task)
    {
        return NULL;
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

    uint32_t flags = interrupt_save_and_disable();

    rr_state.current_ticks++;

    // Update task statistics
    rr_state.current_task->exec_time += 1000000; // 1ms
    rr_state.current_task->total_runtime += 1000000;

    // Verificar si se agotó el time slice
    bool should_preempt = false;

    if (rr_state.current_ticks >= rr_state.time_slice)
    {
        should_preempt = true;
        print("RR: Time slice expired for task PID ");
        print_hex_compact(rr_state.current_task->pid);
        print("\n");
    }

    // También preemptar si hay tareas de mayor prioridad esperando
    if (rr_state.task_count > 1 && rr_state.current_ticks >= (rr_state.time_slice / 2))
    {
        task_t *peek_next = rr_state.ready_queue;
        
        if (peek_next && peek_next->priority > rr_state.current_task->priority)
        {
            should_preempt = true;
            print("RR: Higher priority task available\n");
        }
    }

    if (should_preempt && rr_state.task_count >= 0)
    {
        rr_state.current_ticks = 0;

        task_t *current_task = rr_state.current_task;
        task_t *next_task = rr_pick_next_task();

        if (next_task && next_task != current_task)
        {
            // Re-add current task to queue
            current_task->state = TASK_READY;
            current_task->context_switches++;

            // Switch to next task
            rr_state.current_task = next_task;
            next_task->state = TASK_RUNNING;

            print("RR: Switched from PID ");
            print_hex_compact(current_task->pid);
            print(" to PID ");
            print_hex_compact(next_task->pid);
            print("\n");

            // Re-add current task back to queue for next round
            rr_add_task(current_task);

            extern void switch_context_x64(task_t * current, task_t * next);
            switch_context_x64(current_task, next_task);
        }
    }

    interrupt_restore(flags);
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
        .private_data = &rr_state};