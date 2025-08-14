// kernel/scheduler/scheduler_central.c - NUEVO ARCHIVO
#include "scheduler.h"
#include "scheduler_types.h"
#include "task.h"
#include <print.h>
#include <panic/panic.h>
#include <stddef.h>
#include "../../arch/common/arch_interface.h"

// Estado central del scheduler
static task_t *current_running_task = NULL;
static int scheduler_active = 0;
extern void switch_task(task_t *current, task_t *next);

// ===============================================================================
// API PÚBLICA UNIFICADA
// ===============================================================================

void scheduler_init(void)
{
    // Delegar al nuevo sistema de cascade
    if (scheduler_cascade_init() != 0)
    {
        panic("Failed to initialize scheduler cascade!");
    }

    current_running_task = NULL;
    scheduler_active = 0;

    LOG_OK("Central scheduler initialized");
}

void add_task(task_t *task)
{
    if (!current_scheduler.add_task)
    {
        LOG_ERR("add_task: No scheduler initialized");
        return;
    }

    current_scheduler.add_task(task);
}

void scheduler_tick(void)
{
    if (!scheduler_active || !current_scheduler.task_tick)
    {
        return;
    }

    // Llamar al tick del scheduler actual
    current_scheduler.task_tick();

    // Verificar si necesitamos cambio de contexto
    if (current_scheduler.pick_next_task)
    {
        task_t *next_task = current_scheduler.pick_next_task();

        if (next_task && next_task != current_running_task)
        {
            // Realizar cambio de contexto
            if (current_running_task)
            {
                current_running_task->state = TASK_READY;
            }

            next_task->state = TASK_RUNNING;

            if (current_running_task)
            {
                switch_task(current_running_task, next_task);
            }

            current_running_task = next_task;
        }
    }
}

void scheduler_start(void)
{
    if (!current_scheduler.pick_next_task)
    {
        panic("scheduler_start: No valid scheduler");
    }

    // Obtener primera tarea
    task_t *first_task = current_scheduler.pick_next_task();
    if (!first_task)
    {
        panic("scheduler_start: No tasks to run");
    }

    current_running_task = first_task;
    first_task->state = TASK_RUNNING;
    scheduler_active = 1;

    LOG_OK("Scheduler started with first task");

    // En lugar de salto directo, usar dispatch loop controlado
    scheduler_dispatch_loop();
}

// ===============================================================================
// NUEVO: Dispatch loop que reemplaza el salto directo problemático
// ===============================================================================

void scheduler_dispatch_loop(void)
{
    LOG_OK("Entering scheduler dispatch loop");

    while (1)
    {
        if (current_running_task)
        {
            // Ejecutar tarea actual
            // NOTA: En un kernel real, esto sería más complejo
            // Por ahora, simplemente yieldeamos control al timer
            arch_enable_interrupts();
            asm volatile("hlt"); // Esperar próxima interrupción de timer
        }
        else
        {
            // No hay tareas, idle
            asm volatile("hlt");
        }
    }
}

// AGREGAR funciones faltantes que usa el resto del código:

const char *get_scheduler_name(void)
{
    return current_scheduler.name ? current_scheduler.name : "None";
}

scheduler_type_t get_active_scheduler(void)
{
    return active_scheduler_type;
}

void force_scheduler_fallback(void)
{
    scheduler_fallback_to_next();
}
