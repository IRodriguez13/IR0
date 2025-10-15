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

void debug_print_task(const char *prefix, const task_t *task) {
    if (!task) {
        serial_print("SERIAL: [DEBUG] Task is NULL\n");
        return;
    }
    
    serial_print("SERIAL: [DEBUG] ");
    serial_print(prefix);
    serial_print(" PID=");
    serial_print_hex32(task->pid);
    serial_print(" RIP=0x");
    serial_print_hex64(task->rip);
    serial_print(" RSP=0x");
    serial_print_hex64(task->rsp);
    serial_print(" CR3=0x");
    serial_print_hex64(task->cr3);
    serial_print("\n");
}

void rr_schedule_next(void)
{
    serial_print("SERIAL: [SCHED] Starting scheduler\n");
    
    if (!rr_head) {
        serial_print("SERIAL: [SCHED] No processes in scheduler queue\n");
        return;
    }

    static int first = 1;
    process_t *prev = current_process;

    // Log estado actual
    if (prev) {
        serial_print("SERIAL: [SCHED] Current process PID=");
        serial_print_hex32(process_pid(prev));
        serial_print(" state=");
        serial_print_hex32(prev->state);
        serial_print("\n");
    } else {
        serial_print("SERIAL: [SCHED] No current process\n");
    }

    // Seleccionar siguiente proceso
    if (!rr_current) {
        serial_print("SERIAL: [SCHED] First process in queue\n");
        rr_current = rr_head;
    } else {
        rr_current = rr_current->next ? rr_current->next : rr_head;
        serial_print("SERIAL: [SCHED] Next process in queue\n");
    }

    process_t *next = rr_current->process;
    
    if (!next) {
        serial_print("SERIAL: [SCHED] ERROR: Next process is NULL\n");
        return;
    }

    // Evitar cambio innecesario
    if (!first && prev == next) {
        serial_print("SERIAL: [SCHED] No context switch needed\n");
        return;
    }

    // Actualizar estados
    if (prev && prev->state == PROCESS_RUNNING) {
        serial_print("SERIAL: [SCHED] Pausing PID=");
        serial_print_hex32(process_pid(prev));
        serial_print("\n");
        prev->state = PROCESS_READY;
    }

    serial_print("SERIAL: [SCHED] Activating PID=");
    serial_print_hex32(process_pid(next));
    serial_print("\n");
    
    next->state = PROCESS_RUNNING;
    current_process = next;

    // Log detallado del cambio
    if (prev) {
        serial_print("SERIAL: [SCHED] Context switch from PID=");
        serial_print_hex32(process_pid(prev));
        serial_print(" to PID=");
        serial_print_hex32(process_pid(next));
        serial_print("\n");

        debug_print_task("Previous task: ", &prev->task);
    }
    debug_print_task("Next task: ", &next->task);

    // Primer cambio de contexto
    if (first) {
        first = 0;
        serial_print("SERIAL: [SCHED] First context switch, jumping to ring3\n");
        
        // Habilitar interrupciones antes del salto
        __asm__ volatile("sti");
        
        // Saltar al proceso de usuario
        jmp_ring3((void *)next->task.rip);

        // No deberíamos llegar aquí
        panic("SERIAL: [PANIC] Returned from jmp_ring3!\n");
    }

    // Cambio de contexto normal
}
