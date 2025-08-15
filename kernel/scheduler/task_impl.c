// kernel/scheduler/task_impl.c - Implementaciones de funciones de tareas

#include "task.h"
#include "scheduler.h"
#include "../../memory/memo_interface.h"
#include <print.h>
#include <panic/panic.h>
#include <string.h>

// ===============================================================================
// VARIABLES GLOBALES
// ===============================================================================

static task_t *idle_task = NULL;
static uint32_t next_pid = 1;
static task_t *task_list = NULL;

// Variable global para la tarea actualmente ejecutándose
task_t *current_running_task = NULL;

// ===============================================================================
// FUNCIÓN IDLE TASK
// ===============================================================================

// Función que ejecuta el idle task - simplemente hace HLT
static void idle_task_function(void *arg)
{
    (void)arg; // Evitar warning de parámetro no usado
    
    // Función simple que solo hace HLT
    asm volatile("hlt");
    
    // Nunca debería llegar aquí
    for (;;)
    {
        asm volatile("hlt");
    }
}

// ===============================================================================
// FUNCIONES DE GESTIÓN DE TAREAS
// ===============================================================================

task_t *create_task(void (*entry)(void *), void *arg, uint8_t priority, int8_t nice)
{
    // Allocar estructura de tarea
    task_t *task = (task_t *)kmalloc(sizeof(task_t));
    if (!task)
    {
        LOG_ERR("create_task: No memory for task structure");
        return NULL;
    }

    // Allocar stack para la tarea
    void *stack = kmalloc(DEFAULT_STACK_SIZE);
    if (!stack)
    {
        LOG_ERR("create_task: No memory for task stack");
        kfree(task);
        return NULL;
    }

    // Inicializar estructura de tarea
    memset(task, 0, sizeof(task_t));
    
    task->pid = next_pid++;
    task->priority = priority;
    task->nice = nice;
    task->state = TASK_READY;
    task->stack_base = stack;
    task->stack_size = DEFAULT_STACK_SIZE;
    task->entry = entry;
    task->entry_arg = arg;
    
    // Configurar stack para la tarea
    uint32_t *stack_ptr = (uint32_t *)((uintptr_t)stack + DEFAULT_STACK_SIZE);
    
    // Alinear stack a 16 bytes (requerimiento x86)
    stack_ptr = (uint32_t *)((uintptr_t)stack_ptr & ~0xF);
    
    // Configurar stack frame simple para context switch
    // El context switch espera: PUSHA (8 registros) + PUSHFD (EFLAGS)
    
    // PUSHFD - EFLAGS
    stack_ptr -= 4;
    *stack_ptr = 0x202; // EFLAGS con interrupts habilitadas
    
    // PUSHA - EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI
    stack_ptr -= 4; // EDI
    *stack_ptr = 0;
    
    stack_ptr -= 4; // ESI
    *stack_ptr = 0;
    
    stack_ptr -= 4; // EBP
    *stack_ptr = 0;
    
    stack_ptr -= 4; // ESP (placeholder)
    *stack_ptr = 0;
    
    stack_ptr -= 4; // EBX
    *stack_ptr = 0;
    
    stack_ptr -= 4; // EDX
    *stack_ptr = 0;
    
    stack_ptr -= 4; // ECX
    *stack_ptr = 0;
    
    stack_ptr -= 4; // EAX
    *stack_ptr = 0;
    
    // EIP de retorno - apuntar a la función de entrada
    stack_ptr -= 4;
    *stack_ptr = (uintptr_t)entry;
    
    // Guardar puntero al stack
    task->esp = (uintptr_t)stack_ptr;
    task->ebp = 0; // EBP inicial
    
    // Usar el page directory del kernel por ahora
    task->cr3 = 0; // TODO: Implementar page directories por proceso
    
    // Agregar a lista global de tareas
    task->next = task_list;
    task_list = task;
    
    LOG_OK("Task created: PID=%u, priority=%u, nice=%d");
    print_hex_compact(task->pid);
    print_hex_compact(task->priority);
    print_hex_compact(task->nice);
    
    return task;
}

void destroy_task(task_t *task)
{
    if (!task)
    {
        return;
    }
    
    // Marcar como terminada
    task->state = TASK_TERMINATED;
    
    // Liberar stack
    if (task->stack_base)
    {
        kfree(task->stack_base);
        task->stack_base = NULL;
    }
    
    // Remover de lista global
    if (task_list == task)
    {
        task_list = task->next;
    }
    else
    {
        task_t *current = task_list;
        while (current && current->next != task)
        {
            current = current->next;
        }
        if (current)
        {
            current->next = task->next;
        }
    }
    
    // Liberar estructura
    kfree(task);
}

void task_set_nice(task_t *task, int8_t nice)
{
    if (!task)
    {
        return;
    }
    
    if (nice < MIN_NICE || nice > MAX_NICE)
    {
        LOG_WARN("task_set_nice: Invalid nice value");
        return;
    }
    
    task->nice = nice;
}

void task_get_info(task_t *task)
{
    if (!task)
    {
        LOG_ERR("task_get_info: task is NULL");
        return;
    }
    
    print("Task Info:\n");
    print("  PID: ");
    print_hex_compact(task->pid);
    print("\n");
    
    print("  State: ");
    switch (task->state)
    {
    case TASK_READY:
        print("READY");
        break;
    case TASK_RUNNING:
        print("RUNNING");
        break;
    case TASK_BLOCKED:
        print("BLOCKED");
        break;
    case TASK_TERMINATED:
        print("TERMINATED");
        break;
    default:
        print("UNKNOWN");
        break;
    }
    print("\n");
    
    print("  Priority: ");
    print_hex_compact(task->priority);
    print("\n");
    
    print("  Nice: ");
    print_hex_compact(task->nice);
    print("\n");
}

// ===============================================================================
// IMPLEMENTACIÓN DE FUNCIONES DE TESTING
// ===============================================================================

// Función de test task que hace algo útil
static void test_task_function(void *arg)
{
    int task_id = (int)(uintptr_t)arg;
    
    print("Test task ");
    print_hex_compact(task_id);
    print(" started\n");
    
    // Simular trabajo de la tarea
    for (int i = 0; i < 5; i++)
    {
        print("Task ");
        print_hex_compact(task_id);
        print(" iteration ");
        print_hex_compact(i);
        print("\n");
        
        // Simular trabajo de CPU
        for (volatile int j = 0; j < 1000000; j++) {
            // CPU work
        }
    }
    
    print("Test task ");
    print_hex_compact(task_id);
    print(" completed\n");
}

void create_test_tasks(void)
{
    LOG_OK("Creating test tasks...");
    
    // Crear idle task primero
    idle_task = create_task(idle_task_function, NULL, 0, 0);
    if (!idle_task)
    {
        panic("Failed to create idle task!");
    }
    
    // Crear tareas de prueba más interesantes
    task_t *test_task1 = create_task(test_task_function, (void*)1, 1, 0);
    if (!test_task1)
    {
        LOG_WARN("Failed to create test task 1");
    }
    
    task_t *test_task2 = create_task(test_task_function, (void*)2, 2, 1);
    if (!test_task2)
    {
        LOG_WARN("Failed to create test task 2");
    }
    
    task_t *test_task3 = create_task(test_task_function, (void*)3, 3, -1);
    if (!test_task3)
    {
        LOG_WARN("Failed to create test task 3");
    }
    
    // Agregar tareas al scheduler
    add_task(idle_task);
    if (test_task1)
    {
        add_task(test_task1);
    }
    if (test_task2)
    {
        add_task(test_task2);
    }
    if (test_task3)
    {
        add_task(test_task3);
    }
    
    LOG_OK("Test tasks created successfully");
}

// ===============================================================================
// FUNCIONES DE UTILIDAD
// ===============================================================================

task_t *get_idle_task(void)
{
    return idle_task;
}

task_t *get_task_list(void)
{
    return task_list;
}

uint32_t get_task_count(void)
{
    uint32_t count = 0;
    task_t *current = task_list;
    
    while (current)
    {
        if (current->state != TASK_TERMINATED)
        {
            count++;
        }
        current = current->next;
    }
    
    return count;
}
