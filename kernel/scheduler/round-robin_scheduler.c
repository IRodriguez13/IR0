#include "scheduler.h"
#include "task.h"
#include <print.h>
#include <panic/panic.h>
#include <stddef.h>

static task_t *current_task = NULL;
static task_t *ready_queue = NULL;
extern void switch_task(task_t *current, task_t *next);

void scheduler_init()
{
    current_task = NULL;
    ready_queue = NULL;
    LOG_OK("Scheduler inicializado");
}

void add_task(task_t *task)
{
    // Validaciones defensivas
    if (!task)
    {
        LOG_ERR("add_task: task is NULL");
        return;
    }

    if (task->state == TASK_TERMINATED)
    {
        LOG_WARN("add_task: trying to add terminated task");
        return;
    }

    task->state = TASK_READY;

    if (!ready_queue)
    {
        // Primera tarea: crear lista circular de 1 elemento
        ready_queue = task;
        task->next = task;
        LOG_OK("Primera tarea agregada al scheduler");
    }
    else
    {
        // Buscar el último nodo de la lista circular
        task_t *last = ready_queue;

        while (last->next != ready_queue)
        {
            last = last->next;

            // Protección contra listas corruptas
            if (!last || !last->next)
            {
                LOG_ERR("Corrupted ready queue detected!");
                panic("Scheduler corruption detected");
                return;
            }
        }

        // Insertar nueva tarea al final
        last->next = task;
        task->next = ready_queue;
        LOG_OK("Nueva tarea agregada al scheduler");
    }
}

void scheduler_start()
{
    if (!ready_queue)
    {
        LOG_ERR("No hay procesos para ejecutar!");
        panic("Scheduler vacío");
        return;
    }

    // Inicializar current_task con el primer proceso
    current_task = ready_queue;
    current_task->state = TASK_RUNNING;

    LOG_OK("Scheduler iniciado, saltando al primer proceso...");

    // Saltar al primer proceso manualmente
    asm volatile(
        "mov %0, %%esp\n"
        "mov %1, %%ebp\n"
        "jmp *%2"
        :
        : "r"(current_task->esp), "r"(current_task->ebp), "r"(current_task->eip)
        : "memory");
}

void scheduler_tick()
{
    // Verificar si scheduler está listo
    if (!current_task || !ready_queue)
        return;

    __asm__ volatile("cli"); // corto las interrupciones mientras estoy en cambio de contexto.

    // Protección contra corrupción
    if (!current_task->next || current_task->state == TASK_TERMINATED)
    {
        __asm__ volatile("sti"); // Re-enable before return
        LOG_ERR("Task corruption detected!");
        return;
    }

    task_t *next_task = current_task->next;

    // Buscar próximo proceso READY (con protection)
    int attempts = 0;
    while (next_task->state != TASK_READY && next_task != current_task)
    {
        next_task = next_task->next;
        attempts++;

        // ✅ PREVENT infinite loop if all tasks blocked
        if (attempts > MAX_TASKS)
        {
            __asm__ volatile("sti");
            LOG_ERR("All tasks blocked - system deadlock!");
            panic("Scheduler deadlock detected");
            return;
        }
    }

    // Atomic state change
    if (next_task != current_task)
    {
        current_task->state = TASK_READY;
        next_task->state = TASK_RUNNING;

        switch_task(current_task, next_task);
        current_task = next_task;
    }

    __asm__ volatile("sti"); // ✅ Re-enable interrupts
}


void dump_scheduler_state(void)
{
    print_colored("=== SCHEDULER STATE ===\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);

    print("Current task: ");
    if (current_task)
    {
        print_hex_compact(current_task->pid);
        print(" (state: ");
        print_hex_compact(current_task->state);
        print(")");
    }
    else
    {
        print("NULL");
    }
    print("\n");

    print("Ready queue: ");
    if (ready_queue)
    {
        print("Present (starting PID: ");
        print_hex_compact(ready_queue->pid);
        print(")");
    }
    else
    {
        print("Empty");
    }
    print("\n");

    // Contar tareas en la cola
    int task_count = 0;
    if (ready_queue)
    {
        task_t *task = ready_queue;
        do
        {
            task_count++;
            task = task->next;
        } while (task && task != ready_queue && task_count < 100); // Protección anti-loop
    }

    print("Total tasks: ");
    print_hex_compact(task_count);
    print("\n\n");
}

int scheduler_ready(void)
{
    return (ready_queue != NULL && current_task != NULL);
}
