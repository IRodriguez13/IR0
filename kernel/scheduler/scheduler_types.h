// kernel/scheduler/scheduler_types.h
#pragma once
#include <stdint.h>
#include "task.h"

// Scheduler Types - Similar a tu ClockType
typedef enum
{
    SCHEDULER_CFS,         // Completely Fair Scheduler (most sophisticated)
    SCHEDULER_PRIORITY,    // Priority-based with aging
    SCHEDULER_ROUND_ROBIN, // Simple round-robin (fallback)
    SCHEDULER_NONE
} scheduler_type_t;

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
