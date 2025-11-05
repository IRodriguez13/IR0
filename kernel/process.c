#include "process.h"
#include <ir0/memory/kmem.h>
#include <drivers/serial/serial.h>
#include <rr_sched.h>
#include <string.h>

extern void switch_context_x64(task_t *prev, task_t *next);
extern uint64_t get_current_page_directory(void);

// ============================================================
// Variables globales
// ============================================================
process_t *current_process = NULL;
process_t *process_list = NULL;
static pid_t next_pid = 2;

// ============================================================
// Inicialización
// ============================================================
void process_init(void)
{
    current_process = NULL;
    process_list = NULL;
    next_pid = 2;
}

// ============================================================
// Helpers
// ============================================================
pid_t process_get_next_pid(void)
{
    return next_pid++;
}

process_t *process_get_current(void)
{
    return current_process;
}

pid_t process_get_pid(void)
{
    return current_process ? process_pid(current_process) : 0;
}

pid_t process_get_ppid(void)
{
    return current_process ? current_process->ppid : 0;
}

process_t *get_process_list(void)
{
    return process_list;
}

// ============================================================
// Crear un proceso desde una función de entrada
// ============================================================
process_t *process_create(void (*entry)(void))
{
    process_t *proc = kmalloc(sizeof(process_t));
    if (!proc)
        return NULL;

    memset(proc, 0, sizeof(process_t));

    proc->task.pid = next_pid++;
    proc->ppid = 0;
    proc->state = PROCESS_READY;
    proc->page_directory = (uint64_t *)get_current_page_directory();

    // Crear stack
    proc->stack_size = 0x2000;
    proc->stack_start = (uint64_t)kmalloc(proc->stack_size);

    memset((void *)proc->stack_start, 0, proc->stack_size);

    proc->task.rip = (uint64_t)entry;
    proc->task.rsp = proc->stack_start + proc->stack_size - 16;
    proc->task.rbp = proc->task.rsp;
    proc->task.rflags = 0x202;
    proc->task.cs = 0x1B;
    proc->task.ss = 0x23;
    proc->task.ds = 0x23;
    proc->task.es = 0x23;
    proc->task.fs = 0x23;
    proc->task.gs = 0x23;
    proc->task.cr3 = get_current_page_directory();

    // Insertar en lista global
    proc->next = process_list;
    process_list = proc;

    rr_add_process(proc);

    serial_print("Process created PID=");
    serial_print_hex32(proc->task.pid);
    serial_print("\n");

    return proc;
}


pid_t process_fork(void)
{
    if (!current_process) {
        serial_print("fork: no current process\n");
        return -1;
    }

    // 1️⃣ Crear estructura del hijo
    process_t *child = kmalloc(sizeof(process_t));
    if (!child) {
        serial_print("fork: alloc failed\n");
        return -1;
    }

    memcpy(child, current_process, sizeof(process_t));

    // 2️⃣ Asignar PID y PPID
    child->task.pid = next_pid++;
    child->ppid = current_process->task.pid;
    child->state = PROCESS_READY;
    child->exit_code = 0;

    // 3️⃣ Crear nuevo page directory para el hijo
    uint64_t new_cr3 = create_process_page_directory();
    if (!new_cr3) {
        kfree(child);
        return -1;
    }
    child->task.cr3 = new_cr3;
    child->page_directory = (uint64_t *)new_cr3;

    // 4️⃣ Clonar stack en nuevo espacio
    void *new_stack = kmalloc(current_process->stack_size);
    if (!new_stack) {
        kfree(child);
        return -1;
    }
    memcpy(new_stack, (void *)current_process->stack_start, current_process->stack_size);

    // ⚠️ Ajuste de RSP y RBP para stack que crece hacia abajo
    uint64_t offset_rsp = (current_process->stack_start + current_process->stack_size) - current_process->task.rsp;
    uint64_t offset_rbp = (current_process->stack_start + current_process->stack_size) - current_process->task.rbp;

    child->stack_start = (uint64_t)new_stack;
    child->stack_size = current_process->stack_size;
    child->task.rsp = child->stack_start + child->stack_size - offset_rsp;
    child->task.rbp = child->stack_start + child->stack_size - offset_rbp;

    // Alinear stack a 16 bytes
    child->task.rsp &= ~0xF;

    // 5️⃣ Ajustar registros para fork
    child->task.rax = 0;                   // hijo ve 0
    current_process->task.rax = child->task.pid;  // padre ve PID hijo

    // 6️⃣ Insertar en lista de procesos y scheduler
    child->next = process_list;
    process_list = child;

    rr_add_process(child);

    serial_print("Fork: created child PID=");
    serial_print_hex32(child->task.pid);
    serial_print("\n");

    return child->task.pid;
}




// ============================================================
// Exit
// ============================================================
void process_exit(int code)
{
    if (!current_process) return;

    current_process->state = PROCESS_ZOMBIE;
    current_process->exit_code = code;

    serial_print("Process exit PID=");
    serial_print_hex32(current_process->task.pid);
    serial_print(" code=");
    serial_print_hex32(code);
    serial_print("\n");

    for (;;)
        __asm__ volatile("hlt");
}

// ============================================================
// Wait simple (para padre-hijo directo)
// ============================================================
int process_wait(pid_t pid, int *status)
{
    while (1) {
        process_t *p = process_list;
        while (p) {
            if (p->task.pid == pid && p->ppid == current_process->task.pid) {
                if (p->state == PROCESS_ZOMBIE) {
                    if (status) *status = p->exit_code;

                    // Quitar de lista
                    if (process_list == p)
                        process_list = p->next;
                    else {
                        process_t *prev = process_list;
                        while (prev->next != p) prev = prev->next;
                        prev->next = p->next;
                    }

                    pid_t ret = p->task.pid;
                    kfree(p);
                    return ret;
                }
                // Si todavía no es zombie, seguir esperando
                break;
            }
            p = p->next;
        }
        // Ceder CPU a otro proceso mientras esperamos
        rr_schedule_next();
    }
}


// ============================================================
// Copia de directorio de páginas para nuevos procesos
// ============================================================
uint64_t create_process_page_directory(void)
{
    extern void *kmalloc_aligned(size_t size, size_t align);
    uint64_t *pml4 = kmalloc_aligned(4096, 4096);
    if (!pml4) return 0;

    memset(pml4, 0, 4096);
    uint64_t kernel_cr3 = get_current_page_directory();
    uint64_t *kernel_pml4 = (uint64_t *)kernel_cr3;

    for (int i = 256; i < 512; i++)
        pml4[i] = kernel_pml4[i];

    return (uint64_t)pml4;
}

void process_init_fd_table(process_t *process)
{
    if (!process) {
        return;
    }

    for (int i = 0; i < MAX_FDS_PER_PROCESS; i++) {
        process->fd_table[i].in_use = false;
        process->fd_table[i].path[0] = '\0';
        process->fd_table[i].flags = 0;
        process->fd_table[i].offset = 0;
        process->fd_table[i].vfs_file = NULL;
    }

    process->fd_table[0].in_use = true;
    strncpy(process->fd_table[0].path, "/dev/stdin", sizeof(process->fd_table[0].path) - 1);
    process->fd_table[1].in_use = true;
    strncpy(process->fd_table[1].path, "/dev/stdout", sizeof(process->fd_table[1].path) - 1);
    process->fd_table[2].in_use = true;
    strncpy(process->fd_table[2].path, "/dev/stderr", sizeof(process->fd_table[2].path) - 1);
}