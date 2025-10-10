// kernel/scheduler/scheduler.h - VERSIÓN CORREGIDA
#pragma once
#include "scheduler_types.h"


void scheduler_init(void);      
void scheduler_tick(void);      
void add_task(task_t *task);    
void scheduler_start(void);     
void scheduler_main_loop(void); 


void scheduler_dispatch_loop(void);         
void scheduler_yield(void);                  
scheduler_type_t get_active_scheduler(void); 
task_t *get_current_task(void);             
void set_current_task_null(void);           
void terminate_current_task(void);           
const char *get_scheduler_name(void);       
void force_scheduler_fallback(void);         


int scheduler_ready(void);       


#ifndef MAX_TASKS
#define MAX_TASKS 256 // Máximo número de tareas para evitar loops infinitos
#endif

// ===============================================================================
// PROCESS MANAGEMENT INTERFACE  
// ===============================================================================

// Forward declaration for process_t
struct process;

/**
 * Add a process to the scheduler
 * Wrapper around add_task for process_t structures
 */
void scheduler_add_process(struct process *proc);