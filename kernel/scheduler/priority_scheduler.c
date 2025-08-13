// kernel/scheduler/schedulers/priority_scheduler.c
#include "scheduler_types.h"
#include <stddef.h>
#include <print.h>

#define MAX_PRIORITY 140
#define NICE_TO_PRIO(nice) (MAX_PRIORITY / 2 + (nice))

typedef struct priority_runqueue
{
    task_t *priority_lists[MAX_PRIORITY];
    uint32_t priority_bitmap[MAX_PRIORITY / 32 + 1];
    uint32_t nr_running;
} priority_runqueue_t;

static priority_runqueue_t prio_rq;

static void priority_init(void)
{
    LOG_OK("Initializing Priority scheduler");
    for (int i = 0; i < MAX_PRIORITY; i++)
    {
        prio_rq.priority_lists[i] = NULL;
    }
    prio_rq.nr_running = 0;
}

static void priority_add_task(task_t *task)
{
    uint8_t prio = task->priority;
    if (prio >= MAX_PRIORITY)
        prio = MAX_PRIORITY - 1;

    // Add to appropriate priority list
    task->next = prio_rq.priority_lists[prio];
    prio_rq.priority_lists[prio] = task;

    // Set bit in bitmap
    prio_rq.priority_bitmap[prio / 32] |= (1 << (prio % 32));
    prio_rq.nr_running++;
}

static task_t *priority_pick_next_task(void)
{
    // Find highest priority non-empty list
    for (int i = 0; i < MAX_PRIORITY; i++)
    {
        if (prio_rq.priority_lists[i])
        {
            task_t *task = prio_rq.priority_lists[i];
            prio_rq.priority_lists[i] = task->next;

            // If list empty, clear bitmap
            if (!prio_rq.priority_lists[i])
            {
                prio_rq.priority_bitmap[i / 32] &= ~(1 << (i % 32));
            }

            prio_rq.nr_running--;
            return task;
        }
    }
    return NULL;
}

static void priority_task_tick(void)
{
    // Implementación básica de aging
    // TODO: Implementar aging completo más adelante

    // Por ahora, solo decrementar prioridades periódicamente
    static int aging_counter = 0;
    aging_counter++;

    if (aging_counter >= 100)
    { // Cada 100 ticks
        aging_counter = 0;
        // Implementar aging aquí cuando esté listo
        LOG_OK("Priority scheduler: aging tick");
    }
}

scheduler_ops_t priority_scheduler_ops = {
    .type = SCHEDULER_PRIORITY,
    .name = "Priority Scheduler with Aging",
    .init = priority_init,
    .add_task = priority_add_task,
    .pick_next_task = priority_pick_next_task,
    .task_tick = priority_task_tick,
    .cleanup = NULL,
    .private_data = &prio_rq
};