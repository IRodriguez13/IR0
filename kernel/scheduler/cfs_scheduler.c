// kernel/scheduler/schedulers/cfs_scheduler.c
#include "scheduler_types.h"
#include <print.h>
#include <stddef.h>

// CFS-specific data structures
typedef struct cfs_runqueue
{
    // Red-black tree for tasks (ordered by vruntime)
    task_t *rb_root;
    uint64_t min_vruntime;
    uint32_t nr_running;
} cfs_runqueue_t;

static cfs_runqueue_t cfs_rq;

static void cfs_init(void)
{
    LOG_OK("Initializing CFS scheduler");
    cfs_rq.rb_root = NULL;
    cfs_rq.min_vruntime = 0;
    cfs_rq.nr_running = 0;
}

static void cfs_add_task(task_t *task)
{
    LOG_OK("CFS: Adding task to runqueue");
    task->next = NULL;
    task->prev = NULL;

    if (!cfs_rq.rb_root) {
        cfs_rq.rb_root = task;
    } else {
        // Inserción ordenada por vruntime
        task_t *curr = cfs_rq.rb_root;
        task_t *prev = NULL;
        while (curr && curr->vruntime < task->vruntime) {
            prev = curr;
            curr = curr->next;
        }
        task->next = curr;
        task->prev = prev;
        if (prev) prev->next = task;
        else cfs_rq.rb_root = task;
        if (curr) curr->prev = task;
    }

    LOG_OK("CFS: Adding task to runqueue");
    cfs_rq.nr_running++;
}

static task_t *cfs_pick_next_task(void)
{
    if (!cfs_rq.rb_root) return NULL;

    // Escogemos la tarea con menor vruntime
    task_t *task = cfs_rq.rb_root;
    cfs_rq.rb_root = task->next;
    if (cfs_rq.rb_root) cfs_rq.rb_root->prev = NULL;

    task->next = NULL;
    task->prev = NULL;

    LOG_OK("CFS: Picked task (basic implementation)");
    return task;
}


static void cfs_task_tick(void)
{
    if (!current_running_task)
        return;

    // Incrementar vruntime proporcional al tiempo de CPU usado
    current_running_task->vruntime += 1; // usar tu tiempo de tick real

    // Preempt si hay otra tarea con menor vruntime
    if (cfs_rq.rb_root && cfs_rq.rb_root->vruntime < current_running_task->vruntime)
    {
        // Simple: colocar la tarea actual de nuevo en la runqueue
        cfs_add_task(current_running_task);
        current_running_task = NULL; // permitir al dispatcher escoger otra
    }
}


static void cfs_cleanup(void)
{
    LOG_OK("CFS scheduler cleanup");
    task_t *curr = cfs_rq.rb_root;
    while (curr) {
        task_t *next = curr->next;
        curr->next = curr->prev = NULL;
        curr = next;
    }
    cfs_rq.rb_root = NULL;
    cfs_rq.nr_running = 0;
}


// Export CFS operations
scheduler_ops_t cfs_scheduler_ops =
    {
        .type = SCHEDULER_CFS,
        .name = "Completely Fair Scheduler",
        .init = cfs_init,
        .add_task = cfs_add_task,
        .pick_next_task = cfs_pick_next_task,
        .task_tick = cfs_task_tick,
        .cleanup = cfs_cleanup,
        .private_data = &cfs_rq
    };
