// kernel/process/process.c - Implementación del sistema de procesos
#include "process.h"
#include "../../includes/ir0/print.h"
#include "../../includes/ir0/panic/panic.h"
#include "../../memory/memo_interface.h"
#include "../../memory/physical_allocator.h"
#include "../../kernel/scheduler/task.h"
#include <string.h>

// ===============================================================================
// VARIABLES GLOBALES
// ===============================================================================

static process_t *process_list = NULL;      // Lista de todos los procesos
static process_t *ready_queue = NULL;       // Cola de procesos listos
static process_t *sleeping_queue = NULL;    // Cola de procesos durmiendo
static process_t *zombie_queue = NULL;      // Cola de procesos zombie

process_t *current_process = NULL;          // Proceso actualmente ejecutándose
process_t *idle_process = NULL;             // Proceso idle del sistema
uint32_t process_count = 0;                 // Número total de procesos
pid_t next_pid = 1;                         // Siguiente PID disponible

// ===============================================================================
// FUNCIONES AUXILIARES
// ===============================================================================

static void process_add_to_list(process_t *process)
{
    process->next = process_list;
    process->prev = NULL;
    if (process_list) {
        process_list->prev = process;
    }
    process_list = process;
}

static void process_remove_from_list(process_t *process)
{
    if (process->prev) {
        process->prev->next = process->next;
    } 
    else 
    {
        process_list = process->next;
    }
    if (process->next) 
    {
        process->next->prev = process->prev;
    }
}

static void process_add_to_ready_queue(process_t *process)
{
    process->next = ready_queue;
    ready_queue = process;
}

static void process_remove_from_ready_queue(process_t *process)
{
    if (ready_queue == process) {
        ready_queue = process->next;
        return;
    }
    
    process_t *current = ready_queue;
    while (current && current->next != process) {
        current = current->next;
    }
    if (current) {
        current->next = process->next;
    }
}

static void process_add_to_sleeping_queue(process_t *process)
{
    process->next = sleeping_queue;
    sleeping_queue = process;
}

static void process_remove_from_sleeping_queue(process_t *process)
{
    if (sleeping_queue == process) {
        sleeping_queue = process->next;
        return;
    }
    
    process_t *current = sleeping_queue;
    while (current && current->next != process) {
        current = current->next;
    }
    if (current) {
        current->next = process->next;
    }
}

static void process_add_to_zombie_queue(process_t *process)
{
    process->next = zombie_queue;
    zombie_queue = process;
}

static void process_remove_from_zombie_queue(process_t *process)
{
    if (zombie_queue == process) {
        zombie_queue = process->next;
        return;
    }
    
    process_t *current = zombie_queue;
    while (current && current->next != process) {
        current = current->next;
    }
    if (current) {
        current->next = process->next;
    }
}

// ===============================================================================
// INICIALIZACIÓN
// ===============================================================================

void process_init(void)
{
    print("Initializing process management system...\n");
    
    // Limpiar listas
    process_list = NULL;
    ready_queue = NULL;
    sleeping_queue = NULL;
    zombie_queue = NULL;
    current_process = NULL;
    idle_process = NULL;
    process_count = 0;
    next_pid = 1;
    
    // Crear proceso idle del sistema
    idle_process = process_create("idle", NULL, NULL);
    if (!idle_process) 
    {
        panic("process_init: Failed to create idle process");
    }
    
    idle_process->flags |= PROCESS_FLAG_KERNEL;
    idle_process->priority = PRIORITY_IDLE;
    idle_process->state = PROCESS_READY;
    
    current_process = idle_process;
    
    print_success("Process management system initialized\n");
}

// ===============================================================================
// CREACIÓN Y DESTRUCCIÓN DE PROCESOS
// ===============================================================================

process_t *process_create(const char *name, void (*entry_point)(void *), void *arg)
{
    process_t *process = kmalloc(sizeof(process_t));
    if (!process) {
        LOG_ERR("process_create: Failed to allocate process structure");
        return NULL;
    }
    
    // Limpiar estructura
    memset(process, 0, sizeof(process_t));
    
    // Configurar identificación
    process->pid = next_pid++;
    process->ppid = current_process ? current_process->pid : 0;
    process->pgid = process->pid;
    
    if (name) {
        strncpy(process->name, name, MAX_PROCESS_NAME - 1);
        process->name[MAX_PROCESS_NAME - 1] = '\0';
    } else {
        strcpy(process->name, "unnamed");
    }
    
    // Configurar estado inicial
    process->state = PROCESS_NEW;
    process->priority = PRIORITY_NORMAL;
    process->flags = PROCESS_FLAG_KERNEL;  // Por defecto kernel process
    process->exit_code = 0;
    
    // Configurar timing
    process->start_time = 0;  // TODO: Implementar get_time()
    process->last_run = 0;
    process->time_slice = 100;  // 100ms por defecto
    
    // Configurar memoria
    process->page_directory = 0;  // TODO: Implementar page directory creation
    process->kernel_stack = 0;    // TODO: Allocar kernel stack
    process->user_stack = 0;      // TODO: Allocar user stack
    process->heap_start = 0;
    process->heap_end = 0;
    
    // Configurar archivos
    for (int i = 0; i < 16; i++) 
    {
        process->open_files[i] = -1;
    }
    
    // Configurar señales
    process->signal_mask = 0;
    process->pending_signals = 0;
    
    // Configurar argumentos
    process->argc = 0;
    process->envc = 0;
    
    // Agregar a lista principal
    process_add_to_list(process);
    process_count++;
    
    print_success("Process created: PID=");
    print_hex64(process->pid);
    print(", name='");
    print(process->name);
    print("'\n");
    
    return process;
}

void process_destroy(process_t *process)
{
    if (!process) {
        return;
    }
    
    // Remover de todas las listas
    process_remove_from_list(process);
    process_remove_from_ready_queue(process);
    process_remove_from_sleeping_queue(process);
    process_remove_from_zombie_queue(process);
    
    // Liberar recursos
    if (process->page_directory) {
        // TODO: Implementar page directory destruction
    }
    
    if (process->kernel_stack) {
        // TODO: Liberar kernel stack
    }
    
    if (process->user_stack) {
        // TODO: Liberar user stack
    }
    
    // Liberar estructura
    kfree(process);
    process_count--;
    
    print_success("Process destroyed: PID=");
    print_hex64(process->pid);
    print("\n");
}

// ===============================================================================
// CONTROL DE PROCESOS
// ===============================================================================

int process_fork(process_t *parent)
{
    if (!parent) {
        parent = current_process;
    }
    
    if (!parent) {
        print_error("process_fork: No parent process\n");
        return -1;
    }
    
    // Crear proceso hijo
    process_t *child = process_create(parent->name, NULL, NULL);
    if (!child) {
        print_error("process_fork: Failed to create child process\n");
        return -1;
    }
    
    // Copiar configuración del padre
    child->ppid = parent->pid;
    child->priority = parent->priority;
    child->flags = parent->flags;
    
    // TODO: Implementar copy-on-write para memoria
    // TODO: Copiar page directory
    // TODO: Copiar file descriptors
    // TODO: Copiar argumentos y entorno
    
    // Agregar a lista de hijos del padre
    child->sibling = parent->children;
    parent->children = child;
    
    // Cambiar estado a ready
    child->state = PROCESS_READY;
    process_add_to_ready_queue(child);
    
    print_success("Process forked: parent=");
    print_hex64(parent->pid);
    print(", child=");
    print_hex64(child->pid);
    print("\n");
    
    return child->pid;
}

int process_exec(const char *path, char *const argv[], char *const envp[])
{
    if (!current_process) {
        print_error("process_exec: No current process\n");
        return -1;
    }
    
    // TODO: Implementar exec
    // 1. Cargar programa desde path
    // 2. Configurar argumentos y entorno
    // 3. Cambiar entry point
    // 4. Limpiar memoria del proceso
    
    print_success("Process exec: PID=");
    print_hex64(current_process->pid);
    print(", path='");
    print(path);
    print("'\n");
    
    return 0;
}

void process_exit(int exit_code)
{
    if (!current_process) {
        panic("process_exit: No current process");
    }
    
    current_process->exit_code = exit_code;
    current_process->state = PROCESS_ZOMBIE;
    
    // Remover de ready queue
    process_remove_from_ready_queue(current_process);
    
    // Agregar a zombie queue
    process_add_to_zombie_queue(current_process);
    
    print_success("Process exited: PID=");
    print_hex64(current_process->pid);
    print(", exit_code=");
    print_hex64(exit_code);
    print("\n");
    
    // Cambiar a proceso idle
    current_process = idle_process;
}

int process_wait(pid_t pid, int *status)
{
    // TODO: Implementar wait
    // Buscar proceso zombie con PID específico
    // Si se encuentra, liberarlo y retornar exit code
    
    return 0;
}

// ===============================================================================
// SCHEDULING
// ===============================================================================

void process_schedule(void)
{
    if (!ready_queue) {
        // No hay procesos listos, usar idle
        if (current_process != idle_process) {
            process_switch(current_process, idle_process);
        }
        return;
    }
    
    // Seleccionar siguiente proceso (round-robin simple)
    process_t *next = ready_queue;
    process_remove_from_ready_queue(next);
    process_add_to_ready_queue(next);
    
    if (current_process != next) {
        process_switch(current_process, next);
    }
}

void process_yield(void)
{
    if (!current_process) {
        return;
    }
    
    // Cambiar estado a ready
    current_process->state = PROCESS_READY;
    process_add_to_ready_queue(current_process);
    
    // Programar siguiente proceso
    process_schedule();
}

void process_sleep(uint32_t ms)
{
    if (!current_process) {
        return;
    }
    
    current_process->state = PROCESS_SLEEPING;
    process_add_to_sleeping_queue(current_process);
    
    // TODO: Implementar wakeup timer
    
    process_schedule();
}

void process_wakeup(process_t *process)
{
    if (!process || process->state != PROCESS_SLEEPING) {
        return;
    }
    
    process_remove_from_sleeping_queue(process);
    process->state = PROCESS_READY;
    process_add_to_ready_queue(process);
}

// ===============================================================================
// CONTEXT SWITCHING
// ===============================================================================

void process_switch(process_t *from, process_t *to)
{
    if (!from || !to || from == to) {
        return;
    }
    
    // Guardar contexto del proceso actual
    process_save_context(from);
    
    // Cambiar proceso actual
    current_process = to;
    to->state = PROCESS_RUNNING;
    
    // Restaurar contexto del nuevo proceso
    process_restore_context(to);
    
    print_success("Context switch: ");
    print_hex64(from->pid);
    print(" -> ");
    print_hex64(to->pid);
    print("\n");
}

void process_save_context(process_t *process)
{
    if (!process) {
        return;
    }
    
    // TODO: Implementar guardado de registros
    // Esto se hará en assembler específico por arquitectura
}

void process_restore_context(process_t *process)
{
    if (!process) {
        return;
    }
    
    // TODO: Implementar restauración de registros
    // Esto se hará en assembler específico por arquitectura
}

// ===============================================================================
// BÚSQUEDA Y GESTIÓN
// ===============================================================================

process_t *process_find_by_pid(pid_t pid)
{
    process_t *current = process_list;
    while (current) {
        if (current->pid == pid) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

process_t *process_get_current(void)
{
    return current_process;
}

pid_t process_get_pid(void)
{
    return current_process ? current_process->pid : 0;
}

pid_t process_get_ppid(void)
{
    return current_process ? current_process->ppid : 0;
}

// ===============================================================================
// SEÑALES
// ===============================================================================

void process_send_signal(pid_t pid, int signal)
{
    process_t *process = process_find_by_pid(pid);
    if (!process) {
        print_error("process_send_signal: Process ");
        print_hex64(pid);
        print(" not found\n");
        return;
    }
    
    process->pending_signals |= (1 << signal);
    process->flags |= PROCESS_FLAG_SIGNALED;
    
    print_success("Signal sent: PID=");
    print_hex64(pid);
    print(", signal=");
    print_hex64(signal);
    print("\n");
}

void process_handle_signals(process_t *process)
{
    if (!process || !process->pending_signals) {
        return;
    }
    
    // TODO: Implementar manejo de señales
    // Procesar señales pendientes según signal_mask
    
    process->pending_signals = 0;
    process->flags &= ~PROCESS_FLAG_SIGNALED;
}

// ===============================================================================
// DEBUG Y ESTADÍSTICAS
// ===============================================================================

void process_print_info(process_t *process)
{
    if (!process) {
        return;
    }
    
    print_colored("=== Process Info ===\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print("PID: ");
    print_hex64(process->pid);
    print("\n");
    print("Name: ");
    print(process->name);
    print("\n");
    print("State: ");
    print_hex64(process->state);
    print("\n");
    print("Priority: ");
    print_hex64(process->priority);
    print("\n");
    print("Flags: 0x");
    print_hex64(process->flags);
    print("\n");
}

void process_print_all(void)
{
    process_t *current = process_list;
    int count = 0;
    
    print_colored("=== All Processes ===\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    
    while (current && count < 20) {
        print("PID: ");
        print_hex64(current->pid);
        print(" | Name: ");
        print(current->name);
        print(" | State: ");
        print_hex64(current->state);
        print(" | Priority: ");
        print_hex64(current->priority);
        print("\n");
        
        current = current->next;
        count++;
    }
    
    if (current) {
        print("... (more processes)\n");
    }
}

uint32_t process_get_count(void)
{
    return process_count;
}
