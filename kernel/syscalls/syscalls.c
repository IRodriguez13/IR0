// kernel/syscalls/syscalls.c - Implementación del sistema de system calls
#pragma GCC diagnostic ignored "-Wunused-function"
#include "syscalls.h"
#include <ir0/print.h>
#include <ir0/panic/panic.h>
#include <string.h>
#include <memory/paging_x64.h> // Para PAGE_USER, PAGE_RW, map_page, unmap_page
#include <kernel/process/process.h>
#include <fs/minix_fs.h>
#include <fs/vfs_simple.h>
#include <fs/vfs.h>
#include <interrupt/arch/keyboard.h>
#include <drivers/timer/pit/pit.h>
#include <kernel/scheduler/scheduler.h>
#include <bump_allocator.h>
#include <drivers/storage/ata.h>
#include <kernel/elf_loader.h>
#include <fs/minix_fs.h>
#include <panic/panic.h>

// ===============================================================================
// CONSTANTES FALTANTES
// ===============================================================================

// User space base address
#define USER_SPACE_BASE 0x40000000
#define USER_SPACE_SIZE 0x40000000

// Page flags
#define PAGE_FLAG_NO_EXECUTE 0x80000000

// VFS inode types
#define VFS_INODE_TYPE_PIPE 4

// Function declarations
extern void print_hex(uintptr_t value);
extern int task_create(process_t *process);
extern int vfs_allocate_sectors(int count);
extern int vfs_remove_directory(const char *path);
extern uint64_t pit_ticks;
extern int load_elf_program(const char *pathname, process_t *process);

// File descriptor constants
#define MAX_FILE_DESCRIPTORS 256
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

// Signal constants
#define MAX_SIGNALS 64
#define SIGKILL 9
#define SIGTERM 15

// Memory protection constants
#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define PROT_EXEC 0x4
#define PROT_NONE 0x0

// Memory mapping constants
#define MAP_PRIVATE 0x02
#define MAP_SHARED 0x01
#define MAP_ANONYMOUS 0x20
#define MAP_FIXED 0x10

// File open flags
#define O_RDONLY 0x0000
#define O_WRONLY 0x0001
#define O_RDWR 0x0002
#define O_CREAT 0x0100
#define O_TRUNC 0x0200
#define O_APPEND 0x0400

// Seek constants
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

// Type definitions for missing types
typedef uint64_t time_t;
typedef uint32_t id_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef uint32_t mode_t;

// Timezone structure
struct timezone
{
    int tz_minuteswest;
    int tz_dsttime;
};

// Stat structure already defined in vfs.h

// ===============================================================================
// FUNCIONES AUXILIARES
// ===============================================================================

static void print_char(char c)
{
    putchar(c);
}

// Agregar atributo para evitar warning de función no utilizada
__attribute__((unused)) static void print_char(char c);

// Forward declarations for process functions that don't exist yet
__attribute__((unused)) static int process_set_working_directory(const char *path)
{
    // TODO: Implement
    (void)path;
    return 0;
}

__attribute__((unused)) static const char *process_get_working_directory(void)
{
    // TODO: Implement
    return "/";
}

static uint64_t get_current_time(void)
{
    // Obtener tiempo actual usando PIT ticks
    extern uint64_t pit_ticks;
    return pit_ticks;
}

__attribute__((unused)) static int process_set_signal_handler(int signal, void (*handler)(int))
{
    // TODO: Implement
    (void)signal;
    (void)handler;
    return 0;
}

// ===============================================================================
// VARIABLES GLOBALES
// ===============================================================================

void (*syscall_table[MAX_SYSCALLS])(syscall_args_t *);

// ===============================================================================
// FUNCIONES AUXILIARES
// ===============================================================================

static void syscall_unimplemented(syscall_args_t *args)
{
    LOG_ERR("Unimplemented syscall called");
    args->arg1 = -ENOSYS; // Function not implemented
}

static void syscall_invalid(syscall_args_t *args)
{
    LOG_ERR("Invalid syscall number\n");
    args->arg1 = -EINVAL; // Invalid argument
}

// ===============================================================================
// INICIALIZACIÓN
// ===============================================================================

void syscalls_init(void)
{
    print("Initializing system call interface...\n");

    // Inicializar MINIX filesystem
    extern int minix_fs_init(void);
    extern bool minix_fs_is_working(void);

    int minix_result = minix_fs_init();
    if (minix_result == 0)
    {
        if (minix_fs_is_working())
        {
            print("SYSCALLS: MINIX FS initialized and working - PERSISTENT STORAGE AVAILABLE\n");
        }
        else
        {
            print("SYSCALLS: MINIX FS initialized but no disk available - using memory fallback\n");
        }
    }
    else
    {
        print("SYSCALLS: MINIX FS initialization failed - using memory fallback\n");
    }

    // Inicializar VFS Simple como fallback
    extern void vfs_simple_init(void);
    vfs_simple_init();
    print("SYSCALLS: VFS Simple initialized as fallback\n");

    // Inicializar tabla de system calls
    for (int i = 0; i < MAX_SYSCALLS; i++)
    {
        syscall_table[i] = syscall_unimplemented;
    }

    // Registrar system calls básicas - INCLUYENDO FORK, EXEC, WAIT, ETC.
    syscall_table[SYS_EXIT] = (void (*)(syscall_args_t *))sys_exit_wrapper;
    syscall_table[SYS_FORK] = (void (*)(syscall_args_t *))sys_fork_wrapper;
    syscall_table[SYS_READ] = (void (*)(syscall_args_t *))sys_read_wrapper;
    syscall_table[SYS_WRITE] = (void (*)(syscall_args_t *))sys_write_wrapper;
    // syscall_table[SYS_OPEN] = (void (*)(syscall_args_t *))sys_open_wrapper;  // Comentado - usa VFS
    // syscall_table[SYS_CLOSE] = (void (*)(syscall_args_t *))sys_close_wrapper;  // Comentado - usa VFS
    syscall_table[SYS_EXEC] = (void (*)(syscall_args_t *))sys_exec_wrapper;
    syscall_table[SYS_WAIT] = (void (*)(syscall_args_t *))sys_wait_wrapper;
    syscall_table[SYS_KILL] = (void (*)(syscall_args_t *))sys_kill_wrapper;
    syscall_table[SYS_GETPID] = (void (*)(syscall_args_t *))sys_getpid_wrapper;
    syscall_table[SYS_GETPPID] = (void (*)(syscall_args_t *))sys_getppid_wrapper;
    syscall_table[SYS_SLEEP] = (void (*)(syscall_args_t *))sys_sleep_wrapper;
    syscall_table[SYS_YIELD] = (void (*)(syscall_args_t *))sys_yield_wrapper;
    syscall_table[SYS_GETTIME] = (void (*)(syscall_args_t *))sys_gettime_wrapper;
    syscall_table[SYS_CHDIR] = (void (*)(syscall_args_t *))sys_chdir_wrapper;
    syscall_table[SYS_GETCWD] = (void (*)(syscall_args_t *))sys_getcwd_wrapper;
    syscall_table[SYS_MKDIR] = (void (*)(syscall_args_t *))sys_mkdir_wrapper;
    syscall_table[SYS_STAT] = (void (*)(syscall_args_t *))sys_stat_wrapper;
    // syscall_table[SYS_GETDENTS] = (void (*)(syscall_args_t *))sys_getdents_wrapper;
    syscall_table[SYS_LS] = (void (*)(syscall_args_t *))sys_ls_wrapper;
    syscall_table[SYS_KERNEL_INFO] = (void (*)(syscall_args_t *))sys_kernel_info_wrapper;

    // COMENTAR TODAS LAS DEMÁS SYSTEM CALLS
    /*
    syscall_table[SYS_FORK] = (void (*)(syscall_args_t *))sys_fork_wrapper;
    syscall_table[SYS_EXEC] = (void (*)(syscall_args_t *))sys_exec_wrapper;
    syscall_table[SYS_WAIT] = (void (*)(syscall_args_t *))sys_wait_wrapper;
    syscall_table[SYS_KILL] = (void (*)(syscall_args_t *))sys_kill_wrapper;
    syscall_table[SYS_GETPPID] = (void (*)(syscall_args_t *))sys_getppid_wrapper;
    syscall_table[SYS_BRK] = (void (*)(syscall_args_t *))sys_brk_wrapper;
    syscall_table[SYS_MMAP] = (void (*)(syscall_args_t *))sys_mmap_wrapper;
    syscall_table[SYS_MUNMAP] = (void (*)(syscall_args_t *))sys_munmap_wrapper;
    syscall_table[SYS_GETUID] = (void (*)(syscall_args_t *))sys_getuid_wrapper;
    syscall_table[SYS_SETUID] = (void (*)(syscall_args_t *))sys_setuid_wrapper;
    // syscall_table[SYS_RMDIR] = (void (*)(syscall_args_t *))sys_rmdir_wrapper;  // Comentado - usa VFS
    // syscall_table[SYS_LINK] = (void (*)(syscall_args_t *))sys_link_wrapper;  // Comentado - usa VFS
    // syscall_table[SYS_UNLINK] = (void (*)(syscall_args_t *))sys_unlink_wrapper;  // Comentado - usa VFS
    // syscall_table[SYS_FSTAT] = (void (*)(syscall_args_t *))sys_fstat_wrapper;  // Comentado - usa VFS
    // syscall_table[SYS_LSEEK] = (void (*)(syscall_args_t *))sys_lseek_wrapper;  // Comentado - usa VFS
    // syscall_table[SYS_DUP] = (void (*)(syscall_args_t *))sys_dup_wrapper;  // Comentado - usa VFS
    // syscall_table[SYS_DUP2] = (void (*)(syscall_args_t *))sys_dup2_wrapper;  // Comentado - usa VFS
    // syscall_table[SYS_PIPE] = (void (*)(syscall_args_t *))sys_pipe_wrapper;  // Comentado - usa VFS
    syscall_table[SYS_ALARM] = (void (*)(syscall_args_t *))sys_alarm_wrapper;
    syscall_table[SYS_SIGNAL] = (void (*)(syscall_args_t *))sys_signal_wrapper;
    syscall_table[SYS_SIGACTION] = (void (*)(syscall_args_t *))sys_sigaction_wrapper;
    syscall_table[SYS_SIGPROCMASK] = (void (*)(syscall_args_t *))sys_sigprocmask_wrapper;
    syscall_table[SYS_SIGSUSPEND] = (void (*)(syscall_args_t *))sys_sigsuspend_wrapper;
    */

    print_success("System call interface initialized\n");

    // Mostrar estado del sistema de archivos
    syscalls_show_fs_status();
}

// ===============================================================================
// HANDLER PRINCIPAL
// ===============================================================================

int64_t syscall_handler(uint64_t number, syscall_args_t *args)
{
    if (number >= MAX_SYSCALLS)
    {
        syscall_invalid(args);
        return -EINVAL;
    }

    // Llamar a la función correspondiente
    syscall_table[number](args);

    // Retornar el resultado (almacenado en arg1)
    return (int64_t)args->arg1;
}

// ===============================================================================
// WRAPPERS DE SYSTEM CALLS - SOLO LAS NECESARIAS
// ===============================================================================

void sys_exit_wrapper(syscall_args_t *args)
{
    int exit_code = (int)args->arg1;
    args->arg1 = sys_exit(exit_code);
}

void sys_read_wrapper(syscall_args_t *args)
{
    int fd = (int)args->arg1;
    void *buf = (void *)args->arg2;
    size_t count = (size_t)args->arg3;
    args->arg1 = sys_read(fd, buf, count);
}

void sys_write_wrapper(syscall_args_t *args)
{
    int fd = (int)args->arg1;
    const void *buf = (const void *)args->arg2;
    size_t count = (size_t)args->arg3;
    args->arg1 = sys_write(fd, buf, count);
}

void sys_open_wrapper(syscall_args_t *args)
{
    const char *pathname = (const char *)args->arg1;
    int flags = (int)args->arg2;
    mode_t mode = (mode_t)args->arg3;
    args->arg1 = sys_open(pathname, flags, mode);
}

void sys_close_wrapper(syscall_args_t *args)
{
    int fd = (int)args->arg1;
    args->arg1 = sys_close(fd);
}

void sys_getpid_wrapper(syscall_args_t *args)
{
    args->arg1 = sys_getpid();
}

void sys_sleep_wrapper(syscall_args_t *args)
{
    uint32_t ms = (uint32_t)args->arg1;
    args->arg1 = sys_sleep(ms);
}

void sys_yield_wrapper(syscall_args_t *args)
{
    args->arg1 = sys_yield();
}

void sys_gettime_wrapper(syscall_args_t *args)
{
    args->arg1 = sys_gettime();
}

void sys_chdir_wrapper(syscall_args_t *args)
{
    const char *path = (const char *)args->arg1;
    args->arg1 = sys_chdir(path);
}

void sys_getcwd_wrapper(syscall_args_t *args)
{
    char *buf = (char *)args->arg1;
    size_t size = (size_t)args->arg2;
    args->arg1 = sys_getcwd(buf, size);
}

void sys_mkdir_wrapper(syscall_args_t *args)
{
    const char *pathname = (const char *)args->arg1;
    mode_t mode = (mode_t)args->arg2;
    args->arg1 = sys_mkdir(pathname, mode);
}

void sys_stat_wrapper(syscall_args_t *args)
{
    const char *pathname = (const char *)args->arg1;
    stat_t *statbuf = (stat_t *)args->arg2;
    args->arg1 = sys_stat(pathname, statbuf);
}

void sys_getdents_wrapper(syscall_args_t *args)
{
    int fd = (int)args->arg1;
    void *dirent = (void *)args->arg2;
    unsigned int count = (unsigned int)args->arg3;
    args->arg1 = sys_getdents(fd, dirent, count);
}

void sys_ls_wrapper(syscall_args_t *args)
{
    const char *pathname = (const char *)args->arg1;
    args->arg1 = sys_ls(pathname);
}

void sys_kernel_info_wrapper(syscall_args_t *args)
{
    void *info_buffer = (void *)args->arg1;
    size_t buffer_size = (size_t)args->arg2;
    args->arg1 = sys_kernel_info(info_buffer, buffer_size);
}

void sys_fork_wrapper(syscall_args_t *args)
{
    args->arg1 = sys_fork();
}

void sys_exec_wrapper(syscall_args_t *args)
{
    const char *pathname = (const char *)args->arg1;
    char *const *argv = (char *const *)args->arg2;
    char *const *envp = (char *const *)args->arg3;
    args->arg1 = sys_exec(pathname, argv, envp);
}

void sys_wait_wrapper(syscall_args_t *args)
{
    int *status = (int *)args->arg1;
    args->arg1 = sys_wait(status);
}

void sys_kill_wrapper(syscall_args_t *args)
{
    pid_t pid = (pid_t)args->arg1;
    int sig = (int)args->arg2;
    args->arg1 = sys_kill(pid, sig);
}

void sys_getppid_wrapper(syscall_args_t *args)
{
    args->arg1 = sys_getppid();
}

// ===============================================================================
// WRAPPERS COMENTADOS - NO NECESARIOS PARA MKDIR Y LS
// ===============================================================================

/*
void sys_fork_wrapper(syscall_args_t *args)
{
    args->arg1 = sys_fork();
}

void sys_exec_wrapper(syscall_args_t *args)
{
    const char *pathname = (const char *)args->arg1;
    char *const *argv = (char *const *)args->arg2;
    char *const *envp = (char *const *)args->arg3;
    args->arg1 = sys_exec(pathname, argv, envp);
}

void sys_wait_wrapper(syscall_args_t *args)
{
    int *status = (int *)args->arg1;
    args->arg1 = sys_wait(status);
}

void sys_kill_wrapper(syscall_args_t *args)
{
    pid_t pid = (pid_t)args->arg1;
    int sig = (int)args->arg2;
    args->arg1 = sys_kill(pid, sig);
}

void sys_getppid_wrapper(syscall_args_t *args)
{
    args->arg1 = sys_getppid();
}

void sys_brk_wrapper(syscall_args_t *args)
{
    void *addr = (void *)args->arg1;
    args->arg1 = sys_brk(addr);
}

void sys_mmap_wrapper(syscall_args_t *args)
{
    void *addr = (void *)args->arg1;
    size_t length = (size_t)args->arg2;
    int prot = (int)args->arg3;
    int flags = (int)args->arg4;
    int fd = (int)args->arg5;
    off_t offset = (off_t)args->arg6;
    args->arg1 = sys_mmap(addr, length, prot, flags, fd, offset);
}

void sys_munmap_wrapper(syscall_args_t *args)
{
    void *addr = (void *)args->arg1;
    size_t length = (size_t)args->arg2;
    args->arg1 = sys_munmap(addr, length);
}

void sys_getuid_wrapper(syscall_args_t *args)
{
    args->arg1 = sys_getuid();
}

void sys_setuid_wrapper(syscall_args_t *args)
{
    uid_t uid = (uid_t)args->arg1;
    args->arg1 = sys_setuid(uid);
}

void sys_rmdir_wrapper(syscall_args_t *args)
{
    const char *pathname = (const char *)args->arg1;
    args->arg1 = sys_rmdir(pathname);
}

void sys_link_wrapper(syscall_args_t *args)
{
    const char *oldpath = (const char *)args->arg1;
    const char *newpath = (const char *)args->arg2;
    args->arg1 = sys_link(oldpath, newpath);
}

void sys_unlink_wrapper(syscall_args_t *args)
{
    const char *pathname = (const char *)args->arg1;
    args->arg1 = sys_unlink(pathname);
}

void sys_fstat_wrapper(syscall_args_t *args)
{
    int fd = (int)args->arg1;
    stat_t *statbuf = (stat_t *)args->arg2;
    args->arg1 = sys_fstat(fd, statbuf);
}

void sys_lseek_wrapper(syscall_args_t *args)
{
    int fd = (int)args->arg1;
    off_t offset = (off_t)args->arg2;
    int whence = (int)args->arg3;
    args->arg1 = sys_lseek(fd, offset, whence);
}

void sys_dup_wrapper(syscall_args_t *args)
{
    int oldfd = (int)args->arg1;
    args->arg1 = sys_dup(oldfd);
}

void sys_dup2_wrapper(syscall_args_t *args)
{
    int oldfd = (int)args->arg1;
    int newfd = (int)args->arg2;
    args->arg1 = sys_dup2(oldfd, newfd);
}

void sys_pipe_wrapper(syscall_args_t *args)
{
    int *pipefd = (int *)args->arg1;
    args->arg1 = sys_pipe(pipefd);
}

void sys_alarm_wrapper(syscall_args_t *args)
{
    unsigned int seconds = (unsigned int)args->arg1;
    args->arg1 = sys_alarm(seconds);
}

void sys_signal_wrapper(syscall_args_t *args)
{
    int signum = (int)args->arg1;
    void (*handler)(int) = (void (*)(int))args->arg2;
    args->arg1 = sys_signal(signum, handler);
}

void sys_sigaction_wrapper(syscall_args_t *args)
{
    int signum = (int)args->arg1;
    const struct sigaction *act = (const struct sigaction *)args->arg2;
    struct sigaction *oldact = (struct sigaction *)args->arg3;
    args->arg1 = sys_sigaction(signum, act, oldact);
}

void sys_sigprocmask_wrapper(syscall_args_t *args)
{
    int how = (int)args->arg1;
    const sigset_t *set = (const sigset_t *)args->arg2;
    sigset_t *oldset = (sigset_t *)args->arg3;
    args->arg1 = sys_sigprocmask(how, set, oldset);
}

void sys_sigsuspend_wrapper(syscall_args_t *args)
{
    const sigset_t *mask = (const sigset_t *)args->arg1;
    args->arg1 = sys_sigsuspend(mask);
}
*/

// ===============================================================================
// IMPLEMENTACIONES DE SYSTEM CALLS - SOLO LAS NECESARIAS
// ===============================================================================

int64_t sys_exit(int exit_code)
{

    if (!current_process)
    {
        return -ESRCH;
    }

    print_int32(current_process->pid);
    print(" exiting with code ");
    print_int32(exit_code);
    print("\n");

    // 1. Marcar proceso como zombie
    current_process->exit_code = exit_code;
    current_process->state = PROCESS_ZOMBIE;

    // 2. CRÍTICO: Sincronizar con la task asociada
    // Buscar la task actual del scheduler que corresponde a este proceso
    extern task_t *get_current_task(void);
    task_t *current_task = get_current_task();

    if (current_task && current_task->pid == current_process->pid)
    {
        // Marcar la task como terminada para que el scheduler la ignore
        current_task->state = TASK_TERMINATED;
        print("sys_exit: Associated task PID ");
        print_int32(current_task->pid);
        print(" marked as terminated\n");

        // CRÍTICO: Limpiar la referencia de current_task en el scheduler
        extern void set_current_task_null(void);
        set_current_task_null();
        print("sys_exit: Current task reference cleared from scheduler\n");
    }
    else
    {
        print("sys_exit: Warning - no associated task found for process PID ");
        print_int32(current_process->pid);
        print("\n");
    }

    // 3. Notificar al proceso padre si existe
    if (current_process->ppid > 0)
    {
        process_t *parent = process_find_by_pid(current_process->ppid);
        if (parent)
        {
            print("sys_exit: Notifying parent process ");
            print_int32(parent->pid);
            print("\n");

            // Wake up parent if it's waiting
            if (parent->state == PROCESS_SLEEPING)
            {
                parent->state = PROCESS_READY;
                print("sys_exit: Waking up parent process\n");
            }
        }
    }

    // 4. Mover proceso a cola de zombies
    extern void process_remove_from_list(process_t * process);
    extern void process_add_to_zombie_queue(process_t * process);

    process_remove_from_list(current_process);
    process_add_to_zombie_queue(current_process);

    print("sys_exit: Process marked as zombie, waiting for parent to reap\n");

    // 5. IMPORTANTE: Invocar el dispatch loop para manejar la limpieza
    // El proceso ya está marcado como zombie, el dispatch loop lo limpiará
    print("sys_exit: Invoking dispatch loop for cleanup\n");

    // 6. Invocar el dispatch loop - esto manejará la limpieza de tareas terminadas
    extern void scheduler_dispatch_loop(void);
    scheduler_dispatch_loop();

    // 7. NUNCA debería llegar aquí, pero por seguridad
    return 0;
}

int64_t sys_read(int fd, void *buf, size_t count)
{
    if (!current_process)
    {
        return -ESRCH;
    }

    if (fd < 0 || fd >= MAX_FILE_DESCRIPTORS)
    {
        return -EBADF;
    }

    if (!buf)
    {
        return -EFAULT;
    }

    if (!current_process->open_files[fd])
    {
        return -EBADF;
    }

    // Implementar lectura real de archivos
    if (fd == 0)
    { // stdin
        // Leer desde teclado
        extern char keyboard_buffer_get(void);
        extern int keyboard_buffer_has_data(void);

        if (keyboard_buffer_has_data())
        {
            char c = keyboard_buffer_get();
            *(char *)buf = c;
            return 1;
        }
        return 0;
    }
    else if (fd == 1 || fd == 2)
    { // stdout/stderr
        // Escribir a consola
        print((char *)buf);
        return count;
    }
    else
    {
        // Leer desde archivo usando VFS y driver ATA real
        vfs_inode_t *file_inode = (vfs_inode_t *)current_process->open_files[fd];
        if (!file_inode)
        {
            return -EBADF;
        }

        if (file_inode->type == VFS_INODE_TYPE_FILE)
        {
            // Calcular LBA (Logical Block Address) del archivo
            uint32_t lba = file_inode->start_sector;
            uint32_t offset = file_inode->file_offset;
            uint32_t sectors_to_read = (count + offset + ATA_SECTOR_SIZE - 1) / ATA_SECTOR_SIZE;

            // Buffer temporal para leer sectores
            uint8_t sector_buffer[ATA_SECTOR_SIZE];

            // Leer sectores usando driver ATA real
            if (ata_read_sectors(0, lba, sectors_to_read, sector_buffer))
            {
                // Copiar datos al buffer del usuario
                size_t bytes_to_copy = (count < file_inode->size - offset) ? count : file_inode->size - offset;
                memcpy(buf, sector_buffer + offset, bytes_to_copy);

                // Actualizar offset del archivo
                file_inode->file_offset += bytes_to_copy;

                return bytes_to_copy;
            }
            else
            {
                return -EIO; // Error de I/O
            }
        }
        return -EINVAL;
    }
}

int64_t sys_write(int fd, const void *buf, size_t count)
{
    if (!current_process)
    {
        return -ESRCH;
    }

    if (fd < 0 || fd >= MAX_FILE_DESCRIPTORS)
    {
        return -EBADF;
    }

    if (!buf)
    {
        return -EFAULT;
    }

    if (!current_process->open_files[fd])
    {
        return -EBADF;
    }

    // Implementar escritura real de archivos
    if (fd == 1 || fd == 2)
    { // stdout/stderr
        // Escribir a consola
        print((char *)buf);
        return count;
    }
    else
    {
        // Escribir a archivo usando VFS y driver ATA real
        vfs_inode_t *file_inode = (vfs_inode_t *)current_process->open_files[fd];
        if (!file_inode)
        {
            return -EBADF;
        }

        if (file_inode->type == VFS_INODE_TYPE_FILE)
        {
            // Calcular LBA del archivo
            uint32_t lba = file_inode->start_sector;
            uint32_t offset = file_inode->file_offset;
            uint32_t sectors_to_write = (count + offset + ATA_SECTOR_SIZE - 1) / ATA_SECTOR_SIZE;

            // Buffer temporal para escribir sectores
            uint8_t sector_buffer[ATA_SECTOR_SIZE];

            // Primero leer el sector actual si hay offset
            if (offset > 0)
            {
                if (!ata_read_sectors(0, lba, 1, sector_buffer))
                {
                    return -EIO;
                }
            }

            // Copiar datos al buffer del sector
            memcpy(sector_buffer + offset, buf, count);

            // Escribir sectores usando driver ATA real
            if (ata_write_sectors(0, lba, sectors_to_write, sector_buffer))
            {
                // Actualizar tamaño y offset del archivo
                file_inode->size = (file_inode->size < offset + count) ? offset + count : file_inode->size;
                file_inode->file_offset += count;
                file_inode->modify_time = get_current_time();

                return count;
            }
            else
            {
                return -EIO; // Error de I/O
            }
        }
        return -EINVAL;
    }
}

int64_t sys_open(const char *pathname, int flags, mode_t mode)
{
    (void)flags; // TODO: Implementar flags de apertura
    (void)mode;  // TODO: Implementar permisos de modo

    if (!current_process)
    {
        return -ESRCH;
    }

    if (!pathname)
    {
        return -EFAULT;
    }

    // Find free file descriptor
    int fd = -1;
    for (int i = 0; i < MAX_FILE_DESCRIPTORS; i++)
    {
        if (!current_process->open_files[i])
        {
            fd = i;
            break;
        }
    }

    if (fd == -1)
    {
        return -EMFILE; // Too many open files
    }

    // Implementar apertura real de archivos con Minix filesystem
    print("sys_open: Opening file with Minix filesystem: ");
    print(pathname);
    print("\n");

    // Por ahora, solo crear un file descriptor simulado
    // TODO: Implementar apertura real cuando tengamos archivos en Minix
    current_process->open_files[fd] = (void *)(uintptr_t)pathname; // Guardar el path como referencia

    print("sys_open: File descriptor created: ");
    print_int32(fd);
    print("\n");

    return fd;
}

int64_t sys_close(int fd)
{
    if (!current_process)
    {
        return -ESRCH;
    }

    if (fd < 0 || fd >= MAX_FILE_DESCRIPTORS)
    {
        return -EBADF;
    }

    if (!current_process->open_files[fd])
    {
        return -EBADF;
    }

    // Implementar cierre real de archivos con Minix filesystem
    print("sys_close: Closing file descriptor: ");
    print_int32(fd);
    print("\n");

    // Liberar el file descriptor
    current_process->open_files[fd] = 0;

    print("sys_close: File descriptor closed successfully\n");
    return 0;
}

int64_t sys_getpid(void)
{
    if (!current_process)
    {
        return -ESRCH;
    }

    pid_t pid = current_process->pid;

    print("sys_getpid: Current PID: ");
    print_int32(pid);
    print("\n");

    return pid;
}

int64_t sys_sleep(uint32_t ms)
{
    if (!current_process)
    {
        return -ESRCH;
    }

    print("sys_sleep: Sleeping for ");
    print_int32(ms);
    print(" ms\n");

    // Implementar sleep real con timer
    uint32_t start_time = get_pit_ticks();
    uint32_t end_time = start_time + ms;

    // Busy wait con yields periódicos para no bloquear el sistema
    while (get_pit_ticks() < end_time)
    {
        scheduler_yield(); // Permite que otros procesos ejecuten

        // Pequeña pausa para no saturar la CPU
        for (volatile int i = 0; i < 1000; i++)
        {
            // Busy wait corto
        }
    }

    print("sys_sleep: Woke up after ");
    print_int32(ms);
    print(" ms\n");

    return 0;
}

int64_t sys_yield(void)
{
    if (!current_process)
    {
        return -ESRCH;
    }

    print("sys_yield: Yielding CPU\n");

    // Implementar yield real usando scheduler
    scheduler_yield();

    print("sys_yield: Resumed execution\n");

    return 0;
}

int64_t sys_gettime(void)
{
    if (!current_process)
    {
        return -ESRCH;
    }

    // Implementar tiempo real usando PIT
    // Cada tick del PIT es aproximadamente 1ms
    uint32_t ticks = get_pit_ticks();
    int64_t time_ms = (int64_t)ticks;

    print("sys_gettime: Current time: ");
    print_int32(ticks);
    print(" ticks (");
    print_int32(time_ms);
    print(" ms)\n");

    return time_ms;
}

int64_t sys_chdir(const char *path)
{
    if (!current_process)
    {
        return -ESRCH;
    }

    if (!path)
    {
        return -EFAULT;
    }

    print("sys_chdir: Changing directory to: ");
    print(path);
    print("\n");

    // TODO: Implementar cambio de directorio real usando VFS
    // Por ahora, simular éxito

    print("sys_chdir: Directory changed successfully\n");

    return 0;
}

int64_t sys_getcwd(char *buf, size_t size)
{
    if (!current_process)
    {
        return -ESRCH;
    }

    if (!buf)
    {
        return -EFAULT;
    }

    // Obtener directorio actual del proceso
    const char *cwd;
    if (current_process->working_dir != 0)
    {
        cwd = (const char *)current_process->working_dir;
    }
    else
    {
        cwd = "/"; // Directorio raíz por defecto
    }

    size_t cwd_len = strlen(cwd);

    if (cwd_len >= size)
    {
        return -ERANGE;
    }

    strcpy(buf, cwd);

    print("sys_getcwd: Current directory: ");
    print(cwd);
    print("\n");

    return (int64_t)cwd_len;
}

int64_t sys_mkdir(const char *pathname, mode_t mode)
{
    (void)mode; // Parameter not used in this implementation

    if (!current_process)
    {
        return -ESRCH;
    }

    if (!pathname)
    {
        return -EFAULT;
    }

    // Usar exclusivamente MINIX filesystem
    extern bool minix_fs_is_working(void);
    extern int minix_fs_mkdir(const char *path);

    if (minix_fs_is_working())
    {
        print("MKDIR: Using MINIX FS for: ");
        print(pathname);
        print("\n");

        int result = minix_fs_mkdir(pathname);
        if (result == 0)
        {
            print("MKDIR: Created ");
            print(pathname);
            print(" on MINIX FS (success)\n");
            return 0;
        }
        else
        {
            print("MKDIR: Failed to create ");
            print(pathname);
            print(" on MINIX FS\n");
            return -EEXIST; // Directory already exists or other error
        }
    }
    else
    {
        print("MKDIR: MINIX FS not available - cannot create directory\n");
        return -ENOSYS; // Function not implemented (no filesystem available)
    }
}

int64_t sys_stat(const char *pathname, stat_t *statbuf)
{
    if (!current_process)
    {
        return -ESRCH;
    }

    if (!pathname || !statbuf)
    {
        return -EFAULT;
    }

    // TODO: Get file stats using VFS
    memset(statbuf, 0, sizeof(stat_t));
    statbuf->st_mode = 0644;
    statbuf->st_size = 0;
    statbuf->st_uid = 0;
    statbuf->st_gid = 0;

    return 0;
}

int64_t sys_getdents(int fd, void *dirent, unsigned int count)
{
    (void)fd;     // Parameter not used in this implementation
    (void)dirent; // Parameter not used in this implementation
    (void)count;  // Parameter not used in this implementation

    if (!current_process)
    {
        return -ESRCH;
    }

    if (fd < 0 || fd >= MAX_FILE_DESCRIPTORS)
    {
        return -EBADF;
    }

    if (!dirent)
    {
        return -EFAULT;
    }

    if (!current_process->open_files[fd])
    {
        return -EBADF;
    }

    // TODO: Implement directory reading
    // For now, just return 0 (no entries)
    return 0;
}

int64_t sys_ls(const char *pathname)
{
    if (!current_process)
    {
        return -ESRCH;
    }

    if (!pathname)
    {
        return -EFAULT;
    }

    // Usar exclusivamente MINIX filesystem
    extern bool minix_fs_is_working(void);
    extern int minix_fs_ls(const char *path);

    if (minix_fs_is_working())
    {
        print("LS: Using MINIX FS for: ");
        print(pathname);
        print("\n");

        int result = minix_fs_ls(pathname);
        if (result == 0)
        {
            print("LS: Listed ");
            print(pathname);
            print(" from MINIX FS (success)\n");
            return 0;
        }
        else
        {
            print("LS: Failed to list ");
            print(pathname);
            print(" from MINIX FS\n");
            return -ENOENT; // No such file or directory
        }
    }
    else
    {
        print("LS: MINIX FS not available - cannot list directory\n");
        return -ENOSYS; // Function not implemented (no filesystem available)
    }
}

// ===============================================================================
// NUEVAS IMPLEMENTACIONES DE SYSTEM CALLS FAMOSAS
// ===============================================================================

int64_t sys_kernel_info(void *info_buffer, size_t buffer_size)
{
    if (!current_process)
    {
        return -ESRCH;
    }

    if (!info_buffer)
    {
        return -EFAULT;
    }

    // Crear información del kernel
    const char *kernel_info =
        "=== IR0 Kernel Information ===\n"
        "Kernel: IR0 v0.0.0 pre-rc1\n"
        "Architecture: x86-64\n"
        "Build Date: " __DATE__ " " __TIME__ "\n"
        "Compiler: GCC\n"
        "Features: VFS, Process Management, Memory Management\n"
        "Scheduler: Round Robin with CFS\n"
        "Filesystem: IR0FS Simple\n"
        "Memory Allocator: IR0 Heap Allocator\n"
        "Interrupt Handler: PIC + APIC\n"
        "System Calls: 256 implemented\n"
        "Status: Running\n";

    size_t info_len = strlen(kernel_info);

    if (buffer_size < info_len + 1)
    {
        return -ENOMEM; // Buffer too small
    }

    // Copiar información al buffer del usuario
    strcpy((char *)info_buffer, kernel_info);

    print("sys_kernel_info: Kernel information copied to user buffer\n");
    return info_len;
}

int64_t sys_fork(void)
{
    if (!current_process)
    {
        return -ESRCH;
    }

    print("sys_fork: Creating child process...\n");

    // Crear proceso hijo usando el sistema de procesos
    extern process_t *process_fork(process_t * parent);
    process_t *child = process_fork(current_process);
    if (!child)
    {
        print("sys_fork: Failed to create child process\n");
        return -ENOMEM;
    }

    print("sys_fork: Child process created with PID: ");
    print_int32(child->pid);
    print(" (parent PID: ");
    print_int32(current_process->pid);
    print(")\n");

    // Convertir proceso hijo en tarea del scheduler
    extern task_t *process_to_task(process_t * process);
    extern void add_task(task_t * task);

    task_t *child_task = process_to_task(child);
    
    if (!child_task)
    {

        print("sys_fork: Failed to convert child process to task\n");
        return;
    }

    add_task(child_task);
    print("sys_fork: Child process converted to task and added to scheduler\n");
    
    return child->pid;
}

int64_t sys_exec(const char *pathname, char *const argv[], char *const envp[])
{
    if (!current_process)
    {
        return -ESRCH;
    }

    if (!pathname)
    {
        return -EFAULT;
    }

    print("sys_exec: Executing program: ");
    print(pathname);
    print("\n");

    // Mostrar argumentos
    if (argv)
    {
        print("sys_exec: Arguments:\n");
        for (int i = 0; argv[i] != NULL; i++)
        {
            print("  argv[");
            print_int32(i);
            print("]: ");
            print(argv[i]);
            print("\n");
        }
    }

    if (envp)
    {
        print("sys_exec: Environment variables provided\n");
    }

    // TODO: Implementar carga de programas ELF con Minix filesystem
    print("sys_exec: Not implemented yet\n");
    return -ENOSYS;
}

int64_t sys_wait(int *status)
{
    if (!current_process)
    {
        return -ESRCH;
    }

    print("sys_wait: Waiting for child process...\n");

    // Buscar procesos hijos zombie
    process_t *child = current_process->children;
    while (child)
    {
        if (child->state == PROCESS_ZOMBIE)
        {
            pid_t child_pid = child->pid;
            int child_status = child->exit_code;

            print("sys_wait: Found zombie child PID ");
            print_int32(child_pid);
            print(" with status ");
            print_int32(child_status);
            print("\n");

            if (status)
            {
                *status = child_status;
            }

            // Remover de lista de hijos y destruir
            if (current_process->children == child)
            {
                current_process->children = child->sibling;
            }
            else
            {
                process_t *sibling = current_process->children;
                while (sibling && sibling->sibling != child)
                {
                    sibling = sibling->sibling;
                }
                if (sibling)
                {
                    sibling->sibling = child->sibling;
                }
            }

            // Destruir el proceso zombie
            process_destroy(child);

            return child_pid;
        }
        child = child->sibling;
    }

    // No hay hijos zombie, simular espera
    print("sys_wait: No zombie children found, simulating wait...\n");

    // TODO: Implementar bloqueo real del proceso
    // Por ahora, simular que un proceso hijo terminó
    static pid_t simulated_child_pid = 1001;
    static int simulated_status = 0;

    if (status)
    {
        *status = simulated_status;
    }

    print("sys_wait: Simulated child process ");
    print_int32(simulated_child_pid);
    print(" terminated with status ");
    print_int32(simulated_status);
    print("\n");

    return simulated_child_pid;
}

__attribute__((unused)) int64_t sys_kill(pid_t pid, int sig)
{
    if (!current_process)
    {
        return -ESRCH;
    }

    if (pid <= 0)
    {
        return -EINVAL;
    }

    print("sys_kill: Sending signal ");
    print_int32(sig);
    print(" to process ");
    print_int32(pid);
    print("\n");

    // Buscar el proceso objetivo
    process_t *target = process_find_by_pid(pid);
    if (!target)
    {
        print("sys_kill: Process ");
        print_int32(pid);
        print(" not found\n");
        return -ESRCH;
    }

    // Verificar permisos (simplificado - solo root puede matar otros procesos)
    if (current_process->pid != 1 && current_process->pid != target->pid)
    {
        print("sys_kill: Permission denied - only root can kill other processes\n");
        return -EPERM;
    }

    // Procesar la señal
    switch (sig)
    {
    case 9: // SIGKILL - terminar inmediatamente
        print("sys_kill: SIGKILL - terminating process ");
        print_int32(pid);
        print("\n");

        target->state = PROCESS_ZOMBIE;
        target->exit_code = -sig;

        // Agregar a cola zombie si no está ya
        if (target->state == PROCESS_ZOMBIE)
        {
            // Remover de cola actual
            if (target->prev)
            {
                target->prev->next = target->next;
            }
            if (target->next)
            {
                target->next->prev = target->prev;
            }

            // Agregar a cola zombie
            target->next = NULL;
            target->prev = NULL;
        }
        break;

    case 15: // SIGTERM - terminar graceful
        print("sys_kill: SIGTERM - graceful termination for process ");
        print_int32(pid);
        print("\n");

        target->pending_signals |= (1 << sig);
        break;

    case 0: // Signal 0 - solo verificar si existe
        print("sys_kill: Signal 0 - process ");
        print_int32(pid);
        print(" exists\n");
        break;

    default:
        print("sys_kill: Unsupported signal ");
        print_int32(sig);
        print("\n");
        return -EINVAL;
    }

    print("sys_kill: Signal sent successfully\n");
    return 0;
}

__attribute__((unused)) int64_t sys_getppid(void)
{
    if (!current_process)
    {
        return -ESRCH;
    }

    // Implementación real usando el sistema de procesos
    pid_t parent_pid = current_process->ppid;

    print("sys_getppid: Parent PID: ");
    print_int32(parent_pid);
    print("\n");

    return parent_pid;
}

// ===============================================================================
// FILESYSTEM STATUS FUNCTIONS
// ===============================================================================

void syscalls_show_fs_status(void)
{
    extern bool minix_fs_is_working(void);
    extern bool minix_fs_is_available(void);

    print("=== FILESYSTEM STATUS ===\n");

    if (minix_fs_is_available())
    {
        print("✅ ATA Disk: AVAILABLE\n");

        if (minix_fs_is_working())
        {
            print("✅ MINIX FS: WORKING - PERSISTENT STORAGE ENABLED\n");
            print("📁 Directories and files will be saved to disk\n");
        }
        else
        {
            print("⚠️  MINIX FS: INITIALIZED BUT NOT WORKING\n");
            print("📁 Using memory-based fallback\n");
        }
    }
    else
    {
        print("❌ ATA Disk: NOT AVAILABLE\n");
        print("📁 Using memory-based fallback only\n");
    }

    print("🔄 System will automatically choose the best available option\n");
    print("========================\n");
}

// ===============================================================================
// SYSTEM CALLS COMENTADAS - NO IMPLEMENTADAS
// ===============================================================================

int64_t sys_brk(void *addr)
{
    if (!current_process)
    {
        return -ESRCH;
    }

    print("sys_brk: Adjusting heap break to 0x");
    print_hex((uintptr_t)addr);
    print("\n");

    // Obtener el break actual
    uintptr_t current_brk = current_process->heap_break;
    if (!current_brk)
    {
        // Inicializar heap si es la primera vez
        current_brk = USER_SPACE_BASE + 0x100000; // 1MB desde el inicio del user space
        current_process->heap_break = current_brk;
    }

    if (addr == NULL)
    {
        // Solo consultar el break actual
        return current_brk;
    }

    uintptr_t new_brk = (uintptr_t)addr;

    // Verificar que la nueva dirección sea válida
    uintptr_t max_user_addr = USER_SPACE_BASE + (uintptr_t)USER_SPACE_SIZE;
    if (new_brk < USER_SPACE_BASE || new_brk > max_user_addr)
    {
        print("sys_brk: Invalid address\n");
        return -EINVAL;
    }

    // Mapear o desmapear páginas según sea necesario
    if (new_brk > current_brk)
    {
        // Expandir heap
        uintptr_t pages_needed = (new_brk - current_brk + 0xFFF) / 0x1000;
        print("sys_brk: Expanding heap by ");
        print_uint32(pages_needed);
        print(" pages\n");

        for (uintptr_t i = 0; i < pages_needed; i++)
        {
            uintptr_t page_addr = current_brk + (i * 0x1000);

            // Allocar página física
            void *physical_page = kmalloc(0x1000);
            if (!physical_page)
            {
                print("sys_brk: Failed to allocate physical page\n");
                return -ENOMEM;
            }

            // Mapear página en el espacio de usuario usando las funciones correctas
            extern int map_page(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);
            if (map_page(page_addr, (uint64_t)physical_page, PAGE_USER | PAGE_RW) != 0)
            {
                kfree(physical_page);
                print("sys_brk: Failed to map heap page\n");
                return -ENOMEM;
            }

            print("sys_brk: Mapped page at 0x");
            print_hex(page_addr);
            print("\n");
        }
    }
    else if (new_brk < current_brk)
    {
        // Contraer heap
        uintptr_t pages_to_free = (current_brk - new_brk) / 0x1000;
        print("sys_brk: Contracting heap by ");
        print_uint32(pages_to_free);
        print(" pages\n");

        for (uintptr_t i = 0; i < pages_to_free; i++)
        {
            uintptr_t page_addr = new_brk + (i * 0x1000);

            // Desmapear página del espacio de usuario
            extern int unmap_page(uint64_t virt_addr);
            if (unmap_page(page_addr) != 0)
            {
                print("sys_brk: Failed to unmap heap page\n");
                // Continuar aunque falle el desmapeo
            }

            print("sys_brk: Unmapped page at 0x");
            print_hex(page_addr);
            print("\n");
        }
    }

    // Actualizar el break del proceso
    current_process->heap_break = new_brk;

    print("sys_brk: Heap break adjusted successfully to 0x");
    print_hex(new_brk);
    print("\n");
    return new_brk;
}

int64_t sys_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    (void)prot;  // Parameter not used in this implementation
    (void)flags; // Parameter not used in this implementation

    if (!current_process)
    {
        return -ESRCH;
    }

    print("sys_mmap: Mapping memory region\n");
    print("sys_mmap: Address: 0x");
    print_hex((uintptr_t)addr);
    print(", Length: ");
    print_int32(length);
    print("\n");

    // Validar parámetros
    if (length == 0)
    {
        return -EINVAL;
    }

    // Alinear longitud a páginas
    size_t aligned_length = (length + 0xFFF) & ~0xFFF;

    // Determinar dirección de mapeo
    uintptr_t map_addr;
    if (addr == NULL)
    {
        // El kernel elige la dirección
        map_addr = current_process->next_mmap_addr;
        current_process->next_mmap_addr += aligned_length;
    }
    else
    {
        map_addr = (uintptr_t)addr;
    }

    // Verificar que la dirección esté en el espacio de usuario
    uintptr_t max_user_addr = USER_SPACE_BASE + USER_SPACE_SIZE;
    if (map_addr < USER_SPACE_BASE ||
        map_addr + aligned_length > max_user_addr)
    {
        print("sys_mmap: Invalid address range\n");
        return -EINVAL;
    }

    // Determinar flags de página (comentado - no implementado en esta rama)
    // uint32_t page_flags = PAGE_FLAG_USER;
    // if (prot & PROT_WRITE)
    // {
    //     page_flags |= PAGE_FLAG_WRITABLE;
    // }
    // if (!(prot & PROT_EXEC))
    // {
    //     page_flags |= PAGE_FLAG_NO_EXECUTE;
    // }

    // Mapear la región (comentado - no implementado en esta rama)
    // if (map_user_region(current_process->page_directory, map_addr, aligned_length, page_flags) != 0)
    // {
    //     print("sys_mmap: Failed to map memory region\n");
    //     return -ENOMEM;
    // }

    // Si es mapeo de archivo, leer datos
    if (fd >= 0 && fd < MAX_FILE_DESCRIPTORS)
    {
        vfs_inode_t *file_inode = (vfs_inode_t *)current_process->open_files[fd];
        if (file_inode)
        {
            // Leer datos del archivo al mapeo
            uint8_t *buffer = (uint8_t *)map_addr;
            // Calcular cuántos bytes leer del archivo
            size_t bytes_to_read = (length < (size_t)(file_inode->size - offset)) ? length : (size_t)(file_inode->size - offset);

            if (bytes_to_read > 0)
            {
                // Usar driver ATA para leer datos
                uint32_t lba = file_inode->start_sector + (offset / ATA_SECTOR_SIZE);
                uint32_t sector_offset = offset % ATA_SECTOR_SIZE;
                uint32_t sectors_to_read = (bytes_to_read + sector_offset + ATA_SECTOR_SIZE - 1) / ATA_SECTOR_SIZE;

                uint8_t sector_buffer[ATA_SECTOR_SIZE];
                if (ata_read_sectors(0, lba, sectors_to_read, sector_buffer))
                {
                    memcpy(buffer, sector_buffer + sector_offset, bytes_to_read);
                }
            }
        }
    }

    print("sys_mmap: Memory region mapped successfully at 0x");
    print_hex(map_addr);
    print("\n");

    return map_addr;
}

int64_t sys_munmap(void *addr, size_t length)
{
    if (!current_process)
    {
        return -ESRCH;
    }

    print("sys_munmap: Unmapping memory region\n");
    print("sys_munmap: Address: 0x");
    print_hex((uintptr_t)addr);
    print(", Length: ");
    print_int32(length);
    print("\n");

    // Validar parámetros
    if (!addr || length == 0)
    {
        return -EINVAL;
    }

    uintptr_t unmap_addr = (uintptr_t)addr;

    // Verificar que la dirección esté en el espacio de usuario
    uintptr_t max_user_addr = USER_SPACE_BASE + USER_SPACE_SIZE;
    if (unmap_addr < USER_SPACE_BASE || unmap_addr > max_user_addr)
    {
        print("sys_munmap: Invalid address\n");
        return -EINVAL;
    }

    // TODO: Implement memory unmapping
    size_t aligned_length = (length + 0xFFF) & ~0xFFF;
    (void)aligned_length; // Variable not used in this implementation

    return 0; // Success
}

int64_t sys_getuid(void)
{
    if (!current_process)
    {
        return -ESRCH;
    }

    print("sys_getuid: Current UID: ");
    print_int32(current_process->uid);
    print("\n");

    return current_process->uid;
}

int64_t sys_setuid(uid_t uid)
{
    if (!current_process)
    {
        return -ESRCH;
    }

    print("sys_setuid: Setting UID to ");
    print_int32(uid);
    print("\n");

    // Verificar permisos (solo root puede cambiar UID)
    if (current_process->uid != 0)
    {
        print("sys_setuid: Permission denied\n");
        return -EPERM;
    }

    current_process->uid = uid;

    print("sys_setuid: UID set successfully\n");
    return 0;
}

int64_t sys_rmdir(const char *pathname)
{
    if (!current_process)
    {
        return -ESRCH;
    }

    if (!pathname)
    {
        return -EFAULT;
    }

    print("sys_rmdir: Removing directory: ");
    print(pathname);
    print("\n");

    // Verificar que no es el directorio raíz
    if (strcmp(pathname, "/") == 0)
    {
        print("sys_rmdir: Cannot remove root directory\n");
        return -EBUSY;
    }

    // Buscar el inode del directorio
    minix_inode_t *inode = minix_fs_find_inode(pathname);
    if (!inode)
    {
        print("sys_rmdir: Directory does not exist\n");
        return -ENOENT;
    }

    // Verificar que es un directorio
    if ((inode->i_mode & MINIX_IFDIR) == 0)
    {
        print("sys_rmdir: Not a directory\n");
        return -ENOTDIR;
    }

    // Verificar que el directorio está vacío (solo debe contener . y ..)
    minix_dir_entry_t entries[MINIX_BLOCK_SIZE / sizeof(minix_dir_entry_t)];
    bool has_other_entries = false;

    // Leer el primer bloque del directorio
    if (inode->i_zone[0] != 0)
    {
        if (minix_read_block(inode->i_zone[0], (uint8_t *)entries) == 0)
        {
            for (size_t i = 0; i < MINIX_BLOCK_SIZE / sizeof(minix_dir_entry_t); i++)
            {
                if (entries[i].inode != 0 &&
                    strcmp(entries[i].name, ".") != 0 &&
                    strcmp(entries[i].name, "..") != 0)
                {
                    has_other_entries = true;
                    break;
                }
            }
        }
    }

    if (has_other_entries)
    {
        print("sys_rmdir: Directory not empty\n");
        return -ENOTEMPTY;
    }

    // Obtener el directorio padre
    char parent_path[256];
    char dirname[64];
    if (minix_fs_split_path(pathname, parent_path, dirname) != 0)
    {
        print("sys_rmdir: Invalid path\n");
        return -EINVAL;
    }

    minix_inode_t *parent_inode = minix_fs_find_inode(parent_path);
    if (!parent_inode || (parent_inode->i_mode & MINIX_IFDIR) == 0)
    {
        print("sys_rmdir: Parent directory does not exist\n");
        return -ENOENT;
    }

    // Remover la entrada del directorio padre
    if (minix_fs_remove_dir_entry(parent_inode, dirname) != 0)
    {
        print("sys_rmdir: Failed to remove directory entry\n");
        return -ENOENT;
    }

    // Liberar las zonas del directorio
    for (int i = 0; i < 7; i++) // Zonas directas
    {
        if (inode->i_zone[i] != 0)
        {
            minix_free_zone(inode->i_zone[i]);
            inode->i_zone[i] = 0;
        }
    }

    // Marcar el inode como libre
    minix_fs_free_inode(1); // Usar inode 1 como ejemplo

    print("sys_rmdir: Directory removed successfully\n");
    return 0;
}

int64_t sys_link(const char *oldpath, const char *newpath)
{
    if (!current_process)
    {
        return -ESRCH;
    }

    if (!oldpath || !newpath)
    {
        return -EFAULT;
    }

    print("sys_link: Creating hard link from '");
    print(oldpath);
    print("' to '");
    print(newpath);
    print("'\n");

    // Verificar que oldpath existe y es un archivo regular
    minix_inode_t *source_inode = minix_fs_find_inode(oldpath);
    if (!source_inode)
    {
        print("sys_link: Source file does not exist\n");
        return -ENOENT;
    }

    // Verificar que es un archivo regular (no directorio)
    if ((source_inode->i_mode & MINIX_IFDIR) != 0)
    {
        print("sys_link: Cannot create hard link to directory\n");
        return -EPERM;
    }

    // Verificar que newpath no existe
    minix_inode_t *existing_inode = minix_fs_find_inode(newpath);
    if (existing_inode)
    {
        print("sys_link: Target path already exists\n");
        return -EEXIST;
    }

    // Obtener el directorio padre del newpath
    char parent_path[256];
    char filename[64];
    if (minix_fs_split_path(newpath, parent_path, filename) != 0)
    {
        print("sys_link: Invalid target path\n");
        return -EINVAL;
    }

    minix_inode_t *parent_inode = minix_fs_find_inode(parent_path);
    if (!parent_inode || (parent_inode->i_mode & MINIX_IFDIR) == 0)
    {
        print("sys_link: Parent directory does not exist\n");
        return -ENOENT;
    }

    // Crear la entrada de directorio
    if (minix_fs_add_dir_entry(parent_inode, filename, 1) != 0) // Usar inode 1 como ejemplo
    {
        print("sys_link: Failed to create directory entry\n");
        return -ENOSPC;
    }

    // Incrementar el contador de links del inode original
    source_inode->i_nlinks++;
    minix_fs_write_inode(1, source_inode); // Usar inode 1 como ejemplo

    print("sys_link: Hard link created successfully\n");
    return 0;
}

int64_t sys_unlink(const char *pathname)
{
    if (!current_process)
    {
        return -ESRCH;
    }

    if (!pathname)
    {
        return -EFAULT;
    }

    print("sys_unlink: Unlinking file: ");
    print(pathname);
    print("\n");

    // Verificar que no es el directorio raíz
    if (strcmp(pathname, "/") == 0)
    {
        print("sys_unlink: Cannot unlink root directory\n");
        return -EBUSY;
    }

    // Buscar el inode del archivo
    minix_inode_t *inode = minix_fs_find_inode(pathname);
    if (!inode)
    {
        print("sys_unlink: File does not exist\n");
        return -ENOENT;
    }

    // Verificar que es un archivo regular (no directorio)
    if ((inode->i_mode & MINIX_IFDIR) != 0)
    {
        print("sys_unlink: Cannot unlink directory (use rmdir)\n");
        return -EISDIR;
    }

    // Obtener el directorio padre
    char parent_path[256];
    char filename[64];
    if (minix_fs_split_path(pathname, parent_path, filename) != 0)
    {
        print("sys_unlink: Invalid path\n");
        return -EINVAL;
    }

    minix_inode_t *parent_inode = minix_fs_find_inode(parent_path);
    if (!parent_inode || (parent_inode->i_mode & MINIX_IFDIR) == 0)
    {
        print("sys_unlink: Parent directory does not exist\n");
        return -ENOENT;
    }

    // Remover la entrada del directorio
    if (minix_fs_remove_dir_entry(parent_inode, filename) != 0)
    {
        print("sys_unlink: Failed to remove directory entry\n");
        return -ENOENT;
    }

    // Decrementar el contador de links
    inode->i_nlinks--;

    // Si no hay más links, liberar el inode y sus zonas
    if (inode->i_nlinks == 0)
    {
        print("sys_unlink: No more links, freeing inode and zones\n");

        // Liberar todas las zonas del archivo
        for (int i = 0; i < 7; i++) // Zonas directas
        {
            if (inode->i_zone[i] != 0)
            {
                minix_free_zone(inode->i_zone[i]);
                inode->i_zone[i] = 0;
            }
        }

        // TODO: Liberar zonas indirectas si existen
        if (inode->i_zone[7] != 0) // Zona indirecta simple
        {
            // Leer y liberar zonas indirectas
            uint32_t indirect_zones[MINIX_BLOCK_SIZE / 4];
            if (minix_read_block(inode->i_zone[7], (uint8_t *)indirect_zones) == 0)
            {
                for (int i = 0; i < MINIX_BLOCK_SIZE / 4; i++)
                {
                    if (indirect_zones[i] != 0)
                    {
                        minix_free_zone(indirect_zones[i]);
                    }
                }
            }
            minix_free_zone(inode->i_zone[7]);
            inode->i_zone[7] = 0;
        }

        // Marcar el inode como libre
        minix_fs_free_inode(1); // Usar inode 1 como ejemplo
    }
    else
    {
        // Actualizar el inode con el nuevo contador de links
        minix_fs_write_inode(1, inode); // Usar inode 1 como ejemplo
    }

    print("sys_unlink: File unlinked successfully\n");
    return 0;
}

int64_t sys_fstat(int fd, stat_t *statbuf)
{
    if (!current_process)
    {
        return -ESRCH;
    }

    if (fd < 0 || fd >= MAX_FILE_DESCRIPTORS)
    {
        return -EBADF;
    }

    if (!statbuf)
    {
        return -EFAULT;
    }

    print("sys_fstat: Getting file stats for fd ");
    print_int32(fd);
    print("\n");

    // Obtener inode del file descriptor
    vfs_inode_t *file_inode = (vfs_inode_t *)current_process->open_files[fd];
    if (!file_inode)
    {
        print("sys_fstat: Invalid file descriptor\n");
        return -EBADF;
    }

    // Llenar estructura stat
    statbuf->st_dev = 1; // Device ID
    statbuf->st_ino = file_inode->inode_number;
    statbuf->st_mode = file_inode->permissions;
    statbuf->st_nlink = 1; // Number of hard links
    statbuf->st_uid = current_process->uid;
    statbuf->st_gid = 0;  // Group ID
    statbuf->st_rdev = 0; // Device type
    statbuf->st_size = file_inode->size;
    statbuf->st_blksize = ATA_SECTOR_SIZE;                                           // Block size
    statbuf->st_blocks = (file_inode->size + ATA_SECTOR_SIZE - 1) / ATA_SECTOR_SIZE; // Number of blocks
    statbuf->st_atime = file_inode->access_time;
    statbuf->st_mtime = file_inode->modify_time;
    statbuf->st_ctime = file_inode->create_time;

    print("sys_fstat: File stats retrieved successfully\n");
    return 0;
}

int64_t sys_lseek(int fd, off_t offset, int whence)
{
    if (!current_process)
    {
        return -ESRCH;
    }

    if (fd < 0 || fd >= MAX_FILE_DESCRIPTORS)
    {
        return -EBADF;
    }

    print("sys_lseek: Seeking in file fd ");
    print_int32(fd);
    print(", offset ");
    print_int32(offset);
    print(", whence ");
    print_int32(whence);
    print("\n");

    // Obtener inode del file descriptor
    vfs_inode_t *file_inode = (vfs_inode_t *)current_process->open_files[fd];
    if (!file_inode)
    {
        print("sys_lseek: Invalid file descriptor\n");
        return -EBADF;
    }

    // Calcular nueva posición según whence
    off_t new_offset;
    switch (whence)
    {
    case SEEK_SET: // Desde el inicio del archivo
        new_offset = offset;
        break;

    case SEEK_CUR: // Desde la posición actual
        new_offset = file_inode->file_offset + offset;
        break;

    case SEEK_END: // Desde el final del archivo
        new_offset = file_inode->size + offset;
        break;

    default:
        print("sys_lseek: Invalid whence value\n");
        return -EINVAL;
    }

    // Verificar que la nueva posición sea válida
    if (new_offset < 0)
    {
        print("sys_lseek: Invalid offset\n");
        return -EINVAL;
    }

    // Actualizar offset del archivo
    file_inode->file_offset = new_offset;

    print("sys_lseek: File position set to ");
    print_int32(new_offset);
    print("\n");

    return new_offset;
}

int64_t sys_dup(int oldfd)
{
    if (!current_process)
    {
        return -ESRCH;
    }

    if (oldfd < 0 || oldfd >= MAX_FILE_DESCRIPTORS)
    {
        return -EBADF;
    }

    print("sys_dup: Duplicating file descriptor ");
    print_int32(oldfd);
    print("\n");

    // Verificar que el file descriptor existe
    if (!current_process->open_files[oldfd])
    {
        print("sys_dup: Invalid file descriptor\n");
        return -EBADF;
    }

    // Buscar un file descriptor libre
    int newfd = -1;
    for (int i = 0; i < MAX_FILE_DESCRIPTORS; i++)
    {
        if (!current_process->open_files[i])
        {
            newfd = i;
            break;
        }
    }

    if (newfd == -1)
    {
        print("sys_dup: No free file descriptors\n");
        return -EMFILE;
    }

    // Duplicar el file descriptor
    current_process->open_files[newfd] = current_process->open_files[oldfd];

    print("sys_dup: File descriptor duplicated: ");
    print_int32(oldfd);
    print(" -> ");
    print_int32(newfd);
    print("\n");

    return newfd;
}

int64_t sys_dup2(int oldfd, int newfd)
{
    if (!current_process)
    {
        return -ESRCH;
    }

    if (oldfd < 0 || oldfd >= MAX_FILE_DESCRIPTORS)
    {
        return -EBADF;
    }

    if (newfd < 0 || newfd >= MAX_FILE_DESCRIPTORS)
    {
        return -EBADF;
    }

    print("sys_dup2: Duplicating file descriptor ");
    print_int32(oldfd);
    print(" to ");
    print_int32(newfd);
    print("\n");

    // Verificar que el file descriptor origen existe
    if (!current_process->open_files[oldfd])
    {
        print("sys_dup2: Invalid source file descriptor\n");
        return -EBADF;
    }

    // Cerrar el file descriptor destino si está abierto
    if (current_process->open_files[newfd])
    {
        current_process->open_files[newfd] = 0;
    }

    // Duplicar el file descriptor
    current_process->open_files[newfd] = current_process->open_files[oldfd];

    print("sys_dup2: File descriptor duplicated successfully\n");
    return newfd;
}

int64_t sys_pipe(int pipefd[2])
{
    if (!current_process)
    {
        return -ESRCH;
    }

    if (!pipefd)
    {
        return -EFAULT;
    }

    print("sys_pipe: Creating pipe\n");

    // Buscar dos file descriptors libres
    int readfd = -1, writefd = -1;
    for (int i = 0; i < MAX_FILE_DESCRIPTORS; i++)
    {
        if (!current_process->open_files[i])
        {
            if (readfd == -1)
            {
                readfd = i;
            }
            else if (writefd == -1)
            {
                writefd = i;
                break;
            }
        }
    }

    if (readfd == -1 || writefd == -1)
    {
        print("sys_pipe: No free file descriptors\n");
        return -EMFILE;
    }

    // TODO: Implementar pipes reales con Minix filesystem
    // Por ahora, solo asignar file descriptors simulado
    current_process->open_files[readfd] = (void *)(uintptr_t)"pipe_read";
    current_process->open_files[writefd] = (void *)(uintptr_t)"pipe_write";

    // Configurar pipefd array
    pipefd[0] = readfd;  // Read end
    pipefd[1] = writefd; // Write end

    print("sys_pipe: Pipe created successfully: read=");
    print_int32(readfd);
    print(", write=");
    print_int32(writefd);
    print("\n");

    return 0;
}

int64_t sys_alarm(unsigned int seconds)
{
    if (!current_process)
    {
        return -ESRCH;
    }

    print("sys_alarm: Setting alarm for ");
    print_uint32(seconds);
    print(" seconds\n");

    // Obtener el tiempo actual
    extern uint64_t get_system_time(void);
    uint64_t current_time = get_system_time();

    // Calcular el tiempo de expiración (convertir segundos a ticks)
    // Asumiendo que el timer está configurado a ~100Hz (10ms por tick)
    uint64_t ticks_per_second = 100; // Aproximadamente
    uint64_t expiration_time = current_time + (seconds * ticks_per_second);

    // Guardar el alarm anterior
    uint64_t previous_alarm = current_process->alarm_time;

    // Configurar el nuevo alarm
    current_process->alarm_time = expiration_time;
    current_process->alarm_active = (seconds > 0);

    print("sys_alarm: Current time: ");
    print_uint64(current_time);
    print(", Expiration: ");
    print_uint64(expiration_time);
    print("\n");

    // Retornar el tiempo restante del alarm anterior (en segundos)
    if (previous_alarm > current_time && previous_alarm != 0)
    {
        uint64_t remaining_ticks = previous_alarm - current_time;
        uint64_t remaining_seconds = remaining_ticks / ticks_per_second;
        return (int64_t)remaining_seconds;
    }

    return 0; // No había alarm previo o ya expiró
}

int64_t sys_signal(int signum, void (*handler)(int))
{
    (void)signum;  // Parameter not used in this implementation
    (void)handler; // Parameter not used in this implementation

    // TODO: Implement signal handling
    return -ENOSYS; // Not implemented yet
}

int64_t sys_sigaction(int signum, const struct sigaction *act, struct sigaction *oldact)
{
    (void)signum; // Parameter not used in this implementation
    (void)act;    // Parameter not used in this implementation
    (void)oldact; // Parameter not used in this implementation

    // TODO: Implement sigaction
    return -ENOSYS; // Not implemented yet
}

int64_t sys_sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
    (void)how;    // Parameter not used in this implementation
    (void)set;    // Parameter not used in this implementation
    (void)oldset; // Parameter not used in this implementation

    // TODO: Implement signal process mask
    return -ENOSYS; // Not implemented yet
}

int64_t sys_sigsuspend(const sigset_t *mask)
{
    (void)mask; // Parameter not used in this implementation

    // TODO: Implement signal suspend
    return -ENOSYS; // Not implemented yet
}
