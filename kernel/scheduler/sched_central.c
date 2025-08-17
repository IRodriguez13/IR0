// kernel/scheduler/scheduler_central.c - NUEVO ARCHIVO
#include "scheduler.h"
#include "scheduler_types.h"
#include "task.h"
#include <print.h>
#include <panic/panic.h>
#include <stddef.h>
#include "../../arch/common/arch_interface.h"

// Estado central del scheduler
extern task_t *current_running_task; // Definida en task_impl.c
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
        // Crear idle task si no hay tareas
        LOG_OK("No tasks available, creating idle task");
        
        // Función de idle task
        void idle_task_func(void *arg) {
            (void)arg;
            print_colored("[IDLE] System entering idle state\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
            
            while (1) {
                // Simular trabajo de CPU idle
                for (volatile int i = 0; i < 1000000; i++) {
                    __asm__ volatile("nop");
                }
                // Pequeña pausa para permitir interrupciones
                cpu_wait();
            }
        }
        
        // Crear y agregar idle task
        task_t *idle_task = create_task(idle_task_func, NULL, 0, 0); // Prioridad más baja
        add_task(idle_task);
        
        // Obtener la idle task
        first_task = current_scheduler.pick_next_task();
        if (!first_task) {
            panic("scheduler_start: Failed to create idle task");
        }
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

    for (;;)
    {
        if (current_running_task)
        {
            // Ejecutar tarea actual
            if (current_running_task->state == TASK_RUNNING)
            {
                // En un kernel real, aquí haríamos el context switch real
                // Por ahora, simular ejecución de la tarea
                if (current_running_task->entry)
                {
                    // Llamar la función de entrada de la tarea
                    current_running_task->entry(current_running_task->entry_arg);
                    
                    // Si la tarea retorna, marcarla como terminada
                    current_running_task->state = TASK_TERMINATED;
                    LOG_OK("Task completed and terminated");
                }
                else
                {
                    // Tarea sin función de entrada (como idle task)
                    // Simular trabajo de CPU
                    for (volatile int i = 0; i < 1000000; i++) {
                        // CPU work simulation
                    }
                }
            }
            
            // Habilitar interrupciones para permitir timer ticks
            arch_enable_interrupts();
            
            // Pequeña pausa para permitir interrupciones
            cpu_wait();
        }
        else
        {
            // No hay tareas ejecutándose, ejecutar idle task
            cpu_wait(); // Si la cpu no tiene laburo, AFK.
        }
    }
}

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

// ===============================================================================
// FUNCIONES ADICIONALES
// ===============================================================================

void scheduler_yield(void)
{
    if (scheduler_active && current_running_task)
    {
        // Forzar un tick del scheduler
        scheduler_tick();
    }
}

// ===============================================================================
// IMPLEMENTACIÓN DE scheduler_ready() - FALTABA ESTA FUNCIÓN
// ===============================================================================

int scheduler_ready(void)
{
    return scheduler_active && current_scheduler.pick_next_task;
}
