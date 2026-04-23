/* kernel/scheduler/task.h - per-task CPU context and scheduler linkage */
#pragma once
#include <stdint.h>
#include <ir0/types.h>

typedef enum
{
    TASK_READY,      /* Listo para ejecutar */
    TASK_RUNNING,  /* En ejecución */
    TASK_BLOCKED,  /* Esperando E/S, mutex, etc. */
    TASK_TERMINATED
} task_state_t;

/*
 * Proceso / hilo del kernel: registros guardados y metadatos mínimos.
 * La política de planificación usa priority y la lista next.
 */
typedef struct task
{
    uint64_t rax;      /* +0x00 */
    uint64_t rbx;      /* +0x08 */
    uint64_t rcx;      /* +0x10 */
    uint64_t rdx;      /* +0x18 */
    uint64_t rsi;      /* +0x20 */
    uint64_t rdi;      /* +0x28 */
    uint64_t r8;       /* +0x30 */
    uint64_t r9;       /* +0x38 */
    uint64_t r10;      /* +0x40 */
    uint64_t r11;      /* +0x48 */
    uint64_t r12;      /* +0x50 */
    uint64_t r13;      /* +0x58 */
    uint64_t r14;      /* +0x60 */
    uint64_t r15;      /* +0x68 */
    uint64_t rsp;      /* +0x70 */
    uint64_t rbp;      /* +0x78 */
    uint64_t rip;      /* +0x80 */
    uint64_t rflags;   /* +0x88 */
    uint16_t cs;       /* +0x90 */
    uint16_t ds;       /* +0x92 */
    uint16_t es;       /* +0x94 */
    uint16_t fs;       /* +0x96 */
    uint16_t gs;       /* +0x98 */
    uint16_t ss;       /* +0x9A */
    uint16_t padding1; /* +0x9C */
    uint32_t padding2; /* +0x9E */
    uint64_t cr0;      /* +0xA0 */
    uint64_t cr2;      /* +0xA8 */
    uint64_t cr3;      /* +0xB0 */
    uint64_t cr4;      /* +0xB8 */
    uint64_t dr0;      /* +0xC0 */
    uint64_t dr1;      /* +0xC8 */
    uint64_t dr2;      /* +0xD0 */
    uint64_t dr3;      /* +0xD8 */
    uint64_t dr6;      /* +0xE0 */
    uint64_t dr7;      /* +0xE8 */

    pid_t pid;
    uint8_t priority;   /* 0-255, mayor = más prioridad */
    task_state_t state;
    struct task *next;  /* Lista de tareas */

    void *stack_base;
    uint32_t stack_size;
    void (*entry)(void *);
    void *entry_arg;

    uint32_t context_switches;
    uint64_t total_runtime;
    uint64_t last_run_time;

} task_t;

#define MAX_TASKS 256
#define DEFAULT_STACK_SIZE (4 * 1024)

#define TASK_INIT(name, prio)              \
    {                                      \
        .pid = 0,                          \
        .priority = (prio),                \
        .state = TASK_READY,               \
        .context_switches = 0,             \
        .total_runtime = 0,                \
        .next = NULL,                      \
    }

#define task_is_ready(t) ((t)->state == TASK_READY)
#define task_is_running(t) ((t)->state == TASK_RUNNING)
#define task_is_blocked(t) ((t)->state == TASK_BLOCKED)
#define task_is_terminated(t) ((t)->state == TASK_TERMINATED)

task_t *create_task(void (*entry)(void *), void *arg, uint8_t priority, int8_t nice);
void destroy_task(task_t *task);
void task_get_info(task_t *task);
void create_test_tasks(void);

extern task_t *current_running_task;

task_t *get_task_list(void);
pid_t get_task_count(void);
