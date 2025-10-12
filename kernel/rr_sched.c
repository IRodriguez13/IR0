#include "process.h"
#include "rr_sched.h"
#include <drivers/serial/serial.h>
#include <ir0/memory/kmem.h>
#include <ir0/print.h>
#include <arch/x86-64/sources/user_mode.h>
#include <ir0/panic/panic.h>

// Globals
rr_task_t *rr_head = NULL;
rr_task_t *rr_tail = NULL;
rr_task_t *rr_current = NULL;
extern void switch_context_x64(void *prev, void *next);

// Add a process to RR queue
void rr_add_process(process_t *proc)
{
    if (!proc)
        return;

    rr_task_t *node = kmalloc(sizeof(rr_task_t));
    if (!node)
    {
        serial_print("RR: failed to allocate node\n");
        return;
    }

    node->process = proc;
    node->next = NULL;

    if (!rr_head)
    {
        rr_head = rr_tail = node;
    }
    else
    {
        rr_tail->next = node;
        rr_tail = node;
    }

    proc->state = PROCESS_READY;
    serial_print("RR: added process PID=");
    serial_print_hex32(process_pid(proc));
    serial_print("\n");
}

void rr_schedule_next(void)
{
    if (!rr_head)
        return;

    // We select the next process
    if (!rr_current)
        rr_current = rr_head;
    else
        rr_current = rr_current->next ? rr_current->next : rr_head;

    current_process = rr_current->process;
    current_process->state = PROCESS_RUNNING;

    serial_print("RR: switching to process PID=");
    serial_print_hex32(process_pid(current_process));
    serial_print("\n");

    // Just for the first time, we use jmp_ring3. I know is ugly but it works
    static int first_time = 1;
    if (first_time)
    {
        first_time = 0;
        jmp_ring3((void *)process_rip(current_process));

        // panic should't be executed
        panic("Returned from jmp_ring3!");
    }

    // Next ones we use switch_context normally
    switch_context_x64(NULL, &current_process->task);
}

