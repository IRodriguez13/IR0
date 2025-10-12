#pragma once
#include <stdint.h>
#include "process.h"

typedef struct rr_task
{
    struct rr_task *next;
    process_t *process;
} rr_task_t;

// Scheduler globals
extern rr_task_t *rr_head;
extern rr_task_t *rr_tail;
extern rr_task_t *rr_current;

// Round-robin API
void rr_add_process(process_t *proc);
void rr_schedule_next(void);
