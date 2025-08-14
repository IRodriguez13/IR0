// kernel/scheduler/scheduler_types.h
#pragma once
#include <stdint.h>
#include "task.h"

// Scheduler Types - similar a Clocktype
typedef enum
{
    SCHEDULER_CFS,         // Completely Fair Scheduler (most sophisticated)
    SCHEDULER_PRIORITY,    // Priority-based with aging
    SCHEDULER_ROUND_ROBIN, // Simple round-robin (fallback)
    SCHEDULER_NONE
} scheduler_type_t;

typedef struct cfs_runqueue
{
    task_t *rb_root;
    uint64_t min_vruntime;
    uint32_t nr_running;
} cfs_runqueue_t;

static cfs_runqueue_t cfs_rq;

typedef struct task
{
    uint32_t pid; // Identificador de proceso
    uint32_t priority;
    uint64_t vruntime; // Virtual runtime para CFS
    struct task *prev; // Puntero a tarea anterior en la runqueue
    struct task *next; // Puntero a tarea siguiente en la runqueue
                       // Enlaces para RB-tree
    struct task *rb_left;
    struct task *rb_right;
    struct task *rb_parent;
    int rb_color;
    void (*entry)(void *);
    void *stack;
    uint32_t state;
} task_t;

// Scheduler interface - Similar a tu timer interface
typedef struct
{
    scheduler_type_t type;
    const char *name;

    // Function pointers for scheduler operations
    void (*init)(void);
    void (*add_task)(task_t *task);
    task_t *(*pick_next_task)(void);
    void (*task_tick)(void);
    void (*cleanup)(void);

    // Scheduler-specific data
    void *private_data;
} scheduler_ops_t;

// Global scheduler state
extern scheduler_ops_t current_scheduler;
extern scheduler_type_t active_scheduler_type;

// Detection and fallback functions
scheduler_type_t detect_best_scheduler(void);
int scheduler_cascade_init(void);
void scheduler_fallback_to_next(void);
