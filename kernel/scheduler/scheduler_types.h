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

typedef enum
{
    RB_RED = 0,
    RB_BLACK = 1
} rb_color_t;


typedef struct rb_node
{
    struct rb_node *parent;
    struct rb_node *left;
    struct rb_node *right;
    rb_color_t color;
    uint64_t key; // vruntime
    task_t *task; // puntero a la tarea
} rb_node_t;

typedef struct cfs_runqueue
{
    rb_node_t *root;
    rb_node_t *leftmost;
    uint64_t clock;
    uint64_t exec_clock;
    uint64_t min_vruntime;
    uint64_t avg_vruntime;
    uint32_t nr_running;
    uint32_t total_weight;
    uint64_t targeted_latency;
    uint64_t min_granularity;
    uint32_t load_avg;
    uint32_t runnable_avg;
} cfs_runqueue_t;

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
