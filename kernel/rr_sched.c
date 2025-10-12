#include "process.h"
#include "rr_sched.h"
#include <drivers/serial/serial.h>
#include <ir0/memory/kmem.h>
#include <ir0/print.h>
#include <arch/x86-64/sources/user_mode.h>
#include <ir0/panic/panic.h>

rr_task_t *rr_head = NULL;
rr_task_t *rr_tail = NULL;
rr_task_t *rr_current = NULL;
extern void switch_context_x64(task_t *prev, task_t *next);

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

    static int first = 1;
    process_t *prev = current_process;

    if (!rr_current)
        rr_current = rr_head;
    else
        rr_current = rr_current->next ? rr_current->next : rr_head;

    process_t *next = rr_current->process;

    if (!first && prev == next)
        return;

    if (prev && prev->state == PROCESS_RUNNING)
        prev->state = PROCESS_READY;

    next->state = PROCESS_RUNNING;
    current_process = next;

    serial_print("RR: switching to PID=");
    serial_print_hex32(next->task.pid);
    serial_print("\n");

    if (first)
    {
        // I know it's ugly but it works
        first = 0;
        serial_print("RR: first switch - jumping to ring3\n");
        __asm__ volatile("sti");
        jmp_ring3((void *)next->task.rip);

        // we should never get here
        panic("Returned from jmp_ring3!");
    }

    if (prev)
    {
        serial_print("RR: switching context from PID=");
        serial_print("RR: switching context from PID=");
        serial_print_hex32(prev->task.pid);
        serial_print(" to PID=");
        serial_print_hex32(next->task.pid);
        serial_print("\n");

        serial_print("Prev RSP=0x");
        serial_print_hex32(prev->task.rsp);
        serial_print(" RIP=0x");
        serial_print_hex32(prev->task.rip);
        serial_print("\n");

        serial_print("Next RSP=0x");
        serial_print_hex32(next->task.rsp);
        serial_print(" RIP=0x");
        serial_print_hex32(next->task.rip);
        serial_print("\n");

        serial_print("Prev CR3=0x");
        serial_print_hex32(prev->task.cr3);
        serial_print(" Next CR3=0x");
        serial_print_hex32(next->task.cr3);
        serial_print("\n");

        serial_print("Prev stack_start=0x");
        serial_print_hex32(prev->stack_start);
        serial_print(" Next stack_start=0x");
        serial_print_hex32(next->stack_start);
        serial_print("\n");

        switch_context_x64(&prev->task, &next->task);

        serial_print("RR: returned from switch_context_x64?\n"); // no debería pasar normalmente
    }
}
