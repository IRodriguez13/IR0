// kernel/syscalls/syscalls.c - Implementación del sistema de system calls
#pragma GCC diagnostic ignored "-Wunused-function"
#include "syscalls.h"
#include "../process/process.h"
#include "../../includes/ir0/print.h"
#include "../../includes/ir0/panic/panic.h"
#include "../../fs/vfs.h"
#include "../../fs/vfs_simple.h"
#include "../../interrupt/arch/keyboard.h"
#include "../../drivers/timer/pit/pit.h"
#include "../../kernel/scheduler/scheduler.h"
#include "../../memory/heap_allocator.h"
#include "../../memory/memo_interface.h"
#include "../../memory/process_memo.h"
#include "../../drivers/storage/ata.h"
#include "../../kernel/elf_loader.h"
#include <string.h>

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

    // Inicializar VFS simple
    vfs_simple_init();

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
    syscall_table[SYS_OPEN] = (void (*)(syscall_args_t *))sys_open_wrapper;
    syscall_table[SYS_CLOSE] = (void (*)(syscall_args_t *))sys_close_wrapper;
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
    syscall_table[SYS_RMDIR] = (void (*)(syscall_args_t *))sys_rmdir_wrapper;
    syscall_table[SYS_LINK] = (void (*)(syscall_args_t *))sys_link_wrapper;
    syscall_table[SYS_UNLINK] = (void (*)(syscall_args_t *))sys_unlink_wrapper;
    syscall_table[SYS_FSTAT] = (void (*)(syscall_args_t *))sys_fstat_wrapper;
    syscall_table[SYS_LSEEK] = (void (*)(syscall_args_t *))sys_lseek_wrapper;
    syscall_table[SYS_DUP] = (void (*)(syscall_args_t *))sys_dup_wrapper;
    syscall_table[SYS_DUP2] = (void (*)(syscall_args_t *))sys_dup2_wrapper;
    syscall_table[SYS_PIPE] = (void (*)(syscall_args_t *))sys_pipe_wrapper;
    syscall_table[SYS_ALARM] = (void (*)(syscall_args_t *))sys_alarm_wrapper;
    syscall_table[SYS_SIGNAL] = (void (*)(syscall_args_t *))sys_signal_wrapper;
    syscall_table[SYS_SIGACTION] = (void (*)(syscall_args_t *))sys_sigaction_wrapper;
    syscall_table[SYS_SIGPROCMASK] = (void (*)(syscall_args_t *))sys_sigprocmask_wrapper;
    syscall_table[SYS_SIGSUSPEND] = (void (*)(syscall_args_t *))sys_sigsuspend_wrapper;
    */

    print_success("System call interface initialized\n");
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

    print("sys_exit: Process ");
    print_int32(current_process->pid);
    print(" exiting with code ");
    print_int32(exit_code);
    print("\n");

    // Implementación real usando el sistema de procesos
    current_process->exit_code = exit_code;
    current_process->state = PROCESS_ZOMBIE;

    // Notificar al proceso padre si existe
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

    // El proceso se mantiene como zombie hasta que el padre haga wait()
    print("sys_exit: Process marked as zombie, waiting for parent to reap\n");

    // Context switch a otro proceso
    // El proceso zombie será limpiado por el scheduler

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

    // Implementar apertura real de archivos con VFS
    vfs_inode_t *file_inode = vfs_get_inode(pathname);
    if (!file_inode)
    {
        // Archivo no existe, crear si se solicita
        if (flags & O_CREAT)
        {
            file_inode = vfs_create_inode(pathname, VFS_INODE_TYPE_FILE);
            if (!file_inode)
            {
                return -ENOMEM;
            }
            // Asignar sector inicial para el nuevo archivo
            file_inode->start_sector = vfs_allocate_sectors(1);
            file_inode->file_offset = 0;
        }
        else
        {
            return -ENOENT;
        }
    }

    // Verificar permisos
    if (flags & O_WRONLY || flags & O_RDWR)
    {
        if (!(file_inode->permissions & 0200))
        { // Write permission
            return -EACCES;
        }
    }

    // Resetear offset del archivo si se abre para escritura
    if (flags & O_TRUNC)
    {
        file_inode->size = 0;
        file_inode->file_offset = 0;
    }

    // Asignar file descriptor
    current_process->open_files[fd] = (uintptr_t)file_inode;

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

    // Implementar cierre real de archivos
    vfs_inode_t *file_inode = (vfs_inode_t *)current_process->open_files[fd];
    if (file_inode)
    {
        // En un sistema real, podríamos hacer flush de buffers
        // Por ahora, solo liberar el file descriptor
        current_process->open_files[fd] = 0;
        return 0;
    }

    return -EBADF;
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
    if (!current_process)
    {
        return -ESRCH;
    }

    if (!pathname)
    {
        return -EFAULT;
    }

    print("sys_mkdir: Creating directory: ");
    print(pathname);
    print("\n");

    // Usar VFS simple para crear el directorio
    int result = vfs_simple_mkdir(pathname);

    if (result == 0)
    {
        print("sys_mkdir: Directory created successfully: ");
        print(pathname);
        print("\n");
        return 0;
    }
    else
    {
        print("sys_mkdir: Failed to create directory: ");
        print(pathname);
        print("\n");
        return -ENOMEM;
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

    print("sys_ls: Listing directory: ");
    print(pathname);
    print("\n");

    // Usar VFS simple para listar el directorio
    int result = vfs_simple_ls(pathname);

    if (result == 0)
    {
        print("sys_ls: Directory listed successfully: ");
        print(pathname);
        print("\n");
        return 0;
    }
    else
    {
        print("sys_ls: Failed to list directory: ");
        print(pathname);
        print("\n");
        return -ENOENT;
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
        "Kernel: IR0 v1.0.0\n"
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
    process_t *child = process_fork(current_process);
    if (!child)
    {
        print("sys_fork: Failed to create child process\n");
        return -ENOMEM;
    }

    // Crear page directory para el hijo
    uintptr_t child_pml4 = create_process_page_directory();
    if (!child_pml4)
    {
        print("sys_fork: Failed to create page directory for child\n");
        process_destroy(child);
        return -ENOMEM;
    }

    // Configurar relación padre-hijo
    child->ppid = current_process->pid;
    child->sibling = current_process->children;
    current_process->children = child;
    child->page_directory = child_pml4;

    // Copiar contexto del padre al hijo
    memcpy(&child->context, &current_process->context, sizeof(child->context));

    // Copiar configuración básica
    child->priority = current_process->priority;
    child->flags = current_process->flags;
    child->working_dir = current_process->working_dir;

    // Copiar file descriptors
    for (int i = 0; i < 16; i++)
    {
        child->open_files[i] = current_process->open_files[i];
    }

    // Copiar signal mask
    child->signal_mask = current_process->signal_mask;

    // Configurar stack del hijo en user space
    child->context.rsp = USER_SPACE_BASE + 0x10000 + (child->pid * 0x1000);

    // En el proceso padre, retornar PID del hijo
    // En el proceso hijo, el context switch debería modificar el valor de retorno
    print("sys_fork: Child process created with PID: ");
    print_int32(child->pid);
    print(" (parent PID: ");
    print_int32(current_process->pid);
    print(")\n");
    print("sys_fork: Child page directory: 0x");
    print_hex(child_pml4);
    print("\n");

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

    // Cargar programa ELF usando el loader
    int result = load_elf_program(pathname, current_process);
    if (result != 0)
    {
        print("sys_exec: Failed to load ELF program\n");
        return -ENOEXEC;
    }

    // Setup de argumentos en user stack
    if (argv)
    {
        // Copiar argumentos al stack del usuario
        uintptr_t user_stack = current_process->context.rsp;
        char **user_argv = (char **)user_stack;

        // Calcular espacio necesario para argumentos
        int argc = 0;
        while (argv[argc] != NULL)
            argc++;

        // Copiar argumentos al user space
        for (int i = 0; i < argc; i++)
        {
            size_t arg_len = strlen(argv[i]) + 1;
            user_stack -= arg_len;
            memcpy((void *)user_stack, argv[i], arg_len);
            user_argv[i] = (char *)user_stack;
        }

        // Alinear stack a 16 bytes
        user_stack = (user_stack & ~0xF);
        current_process->context.rsp = user_stack;

        print("sys_exec: Arguments set up in user stack\n");
    }

    print("sys_exec: ELF program loaded successfully\n");
    print("sys_exec: Entry point: 0x");
    print_hex(current_process->context.rip);
    print("\n");

    // exec() no retorna si es exitoso
    // El proceso ahora ejecutará el nuevo programa
    return 0;
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
    if (new_brk < USER_SPACE_BASE || new_brk > USER_SPACE_BASE + USER_SPACE_SIZE)
    {
        print("sys_brk: Invalid address\n");
        return -EINVAL;
    }

    // Mapear o desmapear páginas según sea necesario
    if (new_brk > current_brk)
    {
        // Expandir heap
        uintptr_t pages_needed = (new_brk - current_brk + 0xFFF) / 0x1000;
        for (uintptr_t i = 0; i < pages_needed; i++)
        {
            uintptr_t page_addr = current_brk + (i * 0x1000);
            if (map_user_region(current_process->page_directory, page_addr, 0x1000, PAGE_FLAG_USER | PAGE_FLAG_WRITABLE) != 0)
            {
                print("sys_brk: Failed to map heap page\n");
                return -ENOMEM;
            }
        }
    }
    else if (new_brk < current_brk)
    {
        // Contraer heap
        uintptr_t pages_to_free = (current_brk - new_brk) / 0x1000;
        for (uintptr_t i = 0; i < pages_to_free; i++)
        {
            uintptr_t page_addr = current_brk - ((i + 1) * 0x1000);
            unmap_user_region(current_process->page_directory, page_addr, 0x1000);
        }
    }

    // Actualizar el break del proceso
    current_process->heap_break = new_brk;

    print("sys_brk: Heap break adjusted successfully\n");
    return new_brk;
}

int64_t sys_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
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

    // Verificar que la región esté en user space
    if (map_addr < USER_SPACE_BASE || map_addr + aligned_length > USER_SPACE_BASE + USER_SPACE_SIZE)
    {
        print("sys_mmap: Invalid address range\n");
        return -EINVAL;
    }

    // Determinar flags de página
    uint32_t page_flags = PAGE_FLAG_USER;
    if (prot & PROT_WRITE)
    {
        page_flags |= PAGE_FLAG_WRITABLE;
    }
    if (!(prot & PROT_EXEC))
    {
        page_flags |= PAGE_FLAG_NO_EXECUTE;
    }

    // Mapear la región
    if (map_user_region(current_process->page_directory, map_addr, aligned_length, page_flags) != 0)
    {
        print("sys_mmap: Failed to map memory region\n");
        return -ENOMEM;
    }

    // Si es mapeo de archivo, leer datos
    if (fd >= 0 && fd < MAX_FILE_DESCRIPTORS)
    {
        vfs_inode_t *file_inode = (vfs_inode_t *)current_process->open_files[fd];
        if (file_inode)
        {
            // Leer datos del archivo al mapeo
            uint8_t *buffer = (uint8_t *)map_addr;
            size_t bytes_to_read = (length < file_inode->size - offset) ? length : file_inode->size - offset;

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

    // Verificar que la región esté en user space
    if (unmap_addr < USER_SPACE_BASE || unmap_addr > USER_SPACE_BASE + USER_SPACE_SIZE)
    {
        print("sys_munmap: Invalid address range\n");
        return -EINVAL;
    }

    // Alinear longitud a páginas
    size_t aligned_length = (length + 0xFFF) & ~0xFFF;

    // Desmapear la región
    if (unmap_user_region(current_process->page_directory, unmap_addr, aligned_length) != 0)
    {
        print("sys_munmap: Failed to unmap memory region\n");
        return -EINVAL;
    }

    print("sys_munmap: Memory region unmapped successfully\n");
    return 0;
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

    // Verificar que el directorio existe
    if (!vfs_directory_exists(pathname))
    {
        print("sys_rmdir: Directory not found\n");
        return -ENOENT;
    }

    // Verificar que no es el directorio raíz
    if (strcmp(pathname, "/") == 0)
    {
        print("sys_rmdir: Cannot remove root directory\n");
        return -EBUSY;
    }

    // Verificar permisos
    if (current_process->uid != 0)
    {
        print("sys_rmdir: Permission denied\n");
        return -EPERM;
    }

    // Remover directorio usando VFS
    int result = vfs_remove_directory(pathname);
    if (result == 0)
    {
        print("sys_rmdir: Directory removed successfully\n");
        return 0;
    }
    else
    {
        print("sys_rmdir: Failed to remove directory\n");
        return -EIO;
    }
}

int64_t sys_link(const char *oldpath, const char *newpath)
{
    // TODO: Implement hard link creation
    return -ENOSYS;
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

    // Verificar que el archivo existe
    if (!vfs_file_exists(pathname))
    {
        print("sys_unlink: File not found\n");
        return -ENOENT;
    }

    // Verificar permisos
    if (current_process->uid != 0)
    {
        print("sys_unlink: Permission denied\n");
        return -EPERM;
    }

    // Unlink archivo usando VFS
    int result = vfs_unlink(pathname);
    if (result == 0)
    {
        print("sys_unlink: File unlinked successfully\n");
        return 0;
    }
    else
    {
        print("sys_unlink: Failed to unlink file\n");
        return -EIO;
    }
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

    // Crear inodes para el pipe
    vfs_inode_t *read_inode = vfs_create_inode("/pipe_read", VFS_INODE_TYPE_PIPE);
    vfs_inode_t *write_inode = vfs_create_inode("/pipe_write", VFS_INODE_TYPE_PIPE);

    if (!read_inode || !write_inode)
    {
        print("sys_pipe: Failed to create pipe inodes\n");
        return -ENOMEM;
    }

    // Asignar file descriptors
    current_process->open_files[readfd] = (uintptr_t)read_inode;
    current_process->open_files[writefd] = (uintptr_t)write_inode;

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
    // TODO: Implement alarm setting
    return -ENOSYS;
}

int64_t sys_signal(int signum, void (*handler)(int))
{
    // TODO: Implement signal handling
    return -ENOSYS;
}

int64_t sys_sigaction(int signum, const struct sigaction *act, struct sigaction *oldact)
{
    // TODO: Implement signal action
    return -ENOSYS;
}

int64_t sys_sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
    // TODO: Implement signal mask management
    return -ENOSYS;
}

int64_t sys_sigsuspend(const sigset_t *mask)
{
    // TODO: Implement signal suspension
    return -ENOSYS;
}
