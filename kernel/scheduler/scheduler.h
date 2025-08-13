// Updated scheduler public API (kernel/scheduler/scheduler.h)
#pragma once
#include "scheduler_types.h"

// Public API - remains the same for compatibility
void scheduler_init(void);    // Now calls scheduler_cascade_init()
void scheduler_tick(void);    // Now calls current_scheduler.task_tick()
void add_task(task_t* task);  // Now calls current_scheduler.add_task()

// New API for scheduler management
scheduler_type_t get_active_scheduler(void);
const char* get_scheduler_name(void);
void force_scheduler_fallback(void);
void debug_scheduler_state(void);