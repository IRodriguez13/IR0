// kernel/syscalls/syscalls.c - Implementación del sistema de system calls
#include "syscalls.h"
#include "../process/process.h"
#include "../../includes/ir0/print.h"
#include "../../includes/ir0/panic/panic.h"
#include "../../fs/vfs.h"
#include <string.h>

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
    args->arg1 = -ENOSYS;  // Function not implemented
}

static void syscall_invalid(syscall_args_t *args)
{
    LOG_ERR("Invalid syscall number");
    args->arg1 = -EINVAL;  // Invalid argument
}

// ===============================================================================
// INICIALIZACIÓN
// ===============================================================================

void syscalls_init(void)
{
    print("Initializing system call interface...\n");
    
    // Inicializar tabla de system calls
    for (int i = 0; i < MAX_SYSCALLS; i++) 
    {
        syscall_table[i] = syscall_unimplemented;
    }
    
    // Registrar system calls básicas
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
    syscall_table[SYS_BRK] = (void (*)(syscall_args_t *))sys_brk_wrapper;
    syscall_table[SYS_MMAP] = (void (*)(syscall_args_t *))sys_mmap_wrapper;
    syscall_table[SYS_MUNMAP] = (void (*)(syscall_args_t *))sys_munmap_wrapper;
    syscall_table[SYS_GETTIME] = (void (*)(syscall_args_t *))sys_gettime_wrapper;
    syscall_table[SYS_GETUID] = (void (*)(syscall_args_t *))sys_getuid_wrapper;
    syscall_table[SYS_SETUID] = (void (*)(syscall_args_t *))sys_setuid_wrapper;
    syscall_table[SYS_CHDIR] = (void (*)(syscall_args_t *))sys_chdir_wrapper;
    syscall_table[SYS_GETCWD] = (void (*)(syscall_args_t *))sys_getcwd_wrapper;
    syscall_table[SYS_MKDIR] = (void (*)(syscall_args_t *))sys_mkdir_wrapper;
    syscall_table[SYS_RMDIR] = (void (*)(syscall_args_t *))sys_rmdir_wrapper;
    syscall_table[SYS_LINK] = (void (*)(syscall_args_t *))sys_link_wrapper;
    syscall_table[SYS_UNLINK] = (void (*)(syscall_args_t *))sys_unlink_wrapper;
    syscall_table[SYS_STAT] = (void (*)(syscall_args_t *))sys_stat_wrapper;
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
// WRAPPERS DE SYSTEM CALLS
// ===============================================================================

void sys_exit_wrapper(syscall_args_t *args)
{
    int exit_code = (int)args->arg1;
    args->arg1 = sys_exit(exit_code);
}

void sys_fork_wrapper(syscall_args_t *args)
{
    args->arg1 = sys_fork();
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

void sys_getpid_wrapper(syscall_args_t *args)
{
    args->arg1 = sys_getpid();
}

void sys_getppid_wrapper(syscall_args_t *args)
{
    args->arg1 = sys_getppid();
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

void sys_gettime_wrapper(syscall_args_t *args)
{
    args->arg1 = sys_gettime();
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

void sys_stat_wrapper(syscall_args_t *args)
{
    const char *pathname = (const char *)args->arg1;
    stat_t *statbuf = (stat_t *)args->arg2;
    args->arg1 = sys_stat(pathname, statbuf);
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

// ===============================================================================
// IMPLEMENTACIÓN DE SYSTEM CALLS BÁSICAS
// ===============================================================================

int64_t sys_exit(int exit_code)
{
    if (!current_process) {
        return -ESRCH;  // No such process
    }
    
    process_exit(exit_code);
    return 0;
}

int64_t sys_fork(void)
{
    if (!current_process) {
        return -ESRCH;  // No such process
    }
    
    return process_fork(current_process);
}

int64_t sys_read(int fd, void *buf, size_t count)
{
    if (!current_process) {
        return -ESRCH;  // No such process
    }
    
    if (fd < 0 || fd >= 16) {
        return -EBADF;  // Bad file descriptor
    }
    
    if (!buf) {
        return -EFAULT;  // Bad address
    }
    
    // TODO: Implementar lectura real de archivos
    // Por ahora, retornar error
    return -ENOSYS;  // Function not implemented
}

int64_t sys_write(int fd, const void *buf, size_t count)
{
    if (!current_process) {
        return -ESRCH;  // No such process
    }
    
    if (fd < 0 || fd >= 16) {
        return -EBADF;  // Bad file descriptor
    }
    
    if (!buf) {
        return -EFAULT;  // Bad address
    }
    
    // TODO: Implementar escritura real de archivos
    // Por ahora, retornar error
    return -ENOSYS;  // Function not implemented
}

int64_t sys_open(const char *pathname, int flags, mode_t mode)
{
    if (!current_process) {
        return -ESRCH;  // No such process
    }
    
    if (!pathname) {
        return -EFAULT;  // Bad address
    }
    
    // TODO: Implementar apertura real de archivos
    // Por ahora, retornar error
    return -ENOSYS;  // Function not implemented
}

int64_t sys_close(int fd)
{
    if (!current_process) {
        return -ESRCH;  // No such process
    }
    
    if (fd < 0 || fd >= 16) {
        return -EBADF;  // Bad file descriptor
    }
    
    // TODO: Implementar cierre real de archivos
    // Por ahora, retornar error
    return -ENOSYS;  // Function not implemented
}

int64_t sys_exec(const char *pathname, char *const argv[], char *const envp[])
{
    if (!current_process) {
        return -ESRCH;  // No such process
    }
    
    if (!pathname) {
        return -EFAULT;  // Bad address
    }
    
    // TODO: Implementar exec real
    // Por ahora, retornar error
    return -ENOSYS;  // Function not implemented
}

int64_t sys_wait(int *status)
{
    if (!current_process) {
        return -ESRCH;  // No such process
    }
    
    // TODO: Implementar wait real
    // Por ahora, retornar error
    return -ENOSYS;  // Function not implemented
}

int64_t sys_kill(pid_t pid, int sig)
{
    if (!current_process) {
        return -ESRCH;  // No such process
    }
    
    if (sig < 0 || sig > 31) {
        return -EINVAL;  // Invalid argument
    }
    
    process_send_signal(pid, sig);
    return 0;
}

int64_t sys_getpid(void)
{
    return process_get_pid();
}

int64_t sys_getppid(void)
{
    return process_get_ppid();
}

int64_t sys_sleep(uint32_t ms)
{
    if (!current_process) {
        return -ESRCH;  // No such process
    }
    
    process_sleep(ms);
    return 0;
}

int64_t sys_yield(void)
{
    if (!current_process) {
        return -ESRCH;  // No such process
    }
    
    process_yield();
    return 0;
}

int64_t sys_brk(void *addr)
{
    if (!current_process) {
        return -ESRCH;  // No such process
    }
    
    // TODO: Implementar cambio de heap
    // Por ahora, retornar error
    return -ENOSYS;  // Function not implemented
}

int64_t sys_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    if (!current_process) {
        return -ESRCH;  // No such process
    }
    
    // TODO: Implementar mmap
    // Por ahora, retornar error
    return -ENOSYS;  // Function not implemented
}

int64_t sys_munmap(void *addr, size_t length)
{
    if (!current_process) {
        return -ESRCH;  // No such process
    }
    
    // TODO: Implementar munmap
    // Por ahora, retornar error
    return -ENOSYS;  // Function not implemented
}

int64_t sys_gettime(void)
{
    // TODO: Implementar obtención de tiempo real
    // Por ahora, retornar 0
    return 0;
}

int64_t sys_getuid(void)
{
    // TODO: Implementar obtención de UID
    // Por ahora, retornar 0 (root)
    return 0;
}

int64_t sys_setuid(uid_t uid)
{
    if (!current_process) {
        return -ESRCH;  // No such process
    }
    
    // TODO: Implementar cambio de UID
    // Por ahora, retornar error
    return -ENOSYS;  // Function not implemented
}

int64_t sys_chdir(const char *path)
{
    if (!current_process) {
        return -ESRCH;  // No such process
    }
    
    if (!path) {
        return -EFAULT;  // Bad address
    }
    
    // TODO: Implementar cambio de directorio
    // Por ahora, retornar error
    return -ENOSYS;  // Function not implemented
}

int64_t sys_getcwd(char *buf, size_t size)
{
    if (!current_process) {
        return -ESRCH;  // No such process
    }
    
    if (!buf) {
        return -EFAULT;  // Bad address
    }
    
    // TODO: Implementar obtención de directorio actual
    // Por ahora, retornar error
    return -ENOSYS;  // Function not implemented
}

int64_t sys_mkdir(const char *pathname, mode_t mode)
{
    if (!current_process) {
        return -ESRCH;  // No such process
    }
    
    if (!pathname) {
        return -EFAULT;  // Bad address
    }
    
    // TODO: Implementar creación de directorio
    // Por ahora, retornar error
    return -ENOSYS;  // Function not implemented
}

int64_t sys_rmdir(const char *pathname)
{
    if (!current_process) {
        return -ESRCH;  // No such process
    }
    
    if (!pathname) {
        return -EFAULT;  // Bad address
    }
    
    // TODO: Implementar eliminación de directorio
    // Por ahora, retornar error
    return -ENOSYS;  // Function not implemented
}

int64_t sys_link(const char *oldpath, const char *newpath)
{
    if (!current_process) {
        return -ESRCH;  // No such process
    }
    
    if (!oldpath || !newpath) {
        return -EFAULT;  // Bad address
    }
    
    // TODO: Implementar creación de enlace duro
    // Por ahora, retornar error
    return -ENOSYS;  // Function not implemented
}

int64_t sys_unlink(const char *pathname)
{
    if (!current_process) {
        return -ESRCH;  // No such process
    }
    
    if (!pathname) {
        return -EFAULT;  // Bad address
    }
    
    // TODO: Implementar eliminación de enlace
    // Por ahora, retornar error
    return -ENOSYS;  // Function not implemented
}

int64_t sys_stat(const char *pathname, stat_t *statbuf)
{
    if (!current_process) {
        return -ESRCH;  // No such process
    }
    
    if (!pathname || !statbuf) {
        return -EFAULT;  // Bad address
    }
    
    // TODO: Implementar obtención de información de archivo
    // Por ahora, retornar error
    return -ENOSYS;  // Function not implemented
}

int64_t sys_fstat(int fd, stat_t *statbuf)
{
    if (!current_process) {
        return -ESRCH;  // No such process
    }
    
    if (fd < 0 || fd >= 16) {
        return -EBADF;  // Bad file descriptor
    }
    
    if (!statbuf) {
        return -EFAULT;  // Bad address
    }
    
    // TODO: Implementar obtención de información de descriptor
    // Por ahora, retornar error
    return -ENOSYS;  // Function not implemented
}

int64_t sys_lseek(int fd, off_t offset, int whence)
{
    if (!current_process) {
        return -ESRCH;  // No such process
    }
    
    if (fd < 0 || fd >= 16) {
        return -EBADF;  // Bad file descriptor
    }
    
    // TODO: Implementar reposicionamiento en archivo
    // Por ahora, retornar error
    return -ENOSYS;  // Function not implemented
}

int64_t sys_dup(int oldfd)
{
    if (!current_process) {
        return -ESRCH;  // No such process
    }
    
    if (oldfd < 0 || oldfd >= 16) {
        return -EBADF;  // Bad file descriptor
    }
    
    // TODO: Implementar duplicación de descriptor
    // Por ahora, retornar error
    return -ENOSYS;  // Function not implemented
}

int64_t sys_dup2(int oldfd, int newfd)
{
    if (!current_process) {
        return -ESRCH;  // No such process
    }
    
    if (oldfd < 0 || oldfd >= 16) {
        return -EBADF;  // Bad file descriptor
    }
    
    if (newfd < 0 || newfd >= 16) {
        return -EBADF;  // Bad file descriptor
    }
    
    // TODO: Implementar duplicación de descriptor a número específico
    // Por ahora, retornar error
    return -ENOSYS;  // Function not implemented
}

int64_t sys_pipe(int pipefd[2])
{
    if (!current_process) {
        return -ESRCH;  // No such process
    }
    
    if (!pipefd) {
        return -EFAULT;  // Bad address
    }
    
    // TODO: Implementar creación de pipe
    // Por ahora, retornar error
    return -ENOSYS;  // Function not implemented
}

int64_t sys_alarm(unsigned int seconds)
{
    if (!current_process) {
        return -ESRCH;  // No such process
    }
    
    // TODO: Implementar alarma
    // Por ahora, retornar error
    return -ENOSYS;  // Function not implemented
}

int64_t sys_signal(int signum, void (*handler)(int))
{
    if (!current_process) {
        return -ESRCH;  // No such process
    }
    
    if (signum < 0 || signum > 31) {
        return -EINVAL;  // Invalid argument
    }
    
    // TODO: Implementar establecimiento de manejador de señal
    // Por ahora, retornar error
    return -ENOSYS;  // Function not implemented
}

int64_t sys_sigaction(int signum, const struct sigaction *act, struct sigaction *oldact)
{
    if (!current_process) {
        return -ESRCH;  // No such process
    }
    
    if (signum < 0 || signum > 31) {
        return -EINVAL;  // Invalid argument
    }
    
    // TODO: Implementar establecimiento de acción de señal
    // Por ahora, retornar error
    return -ENOSYS;  // Function not implemented
}

int64_t sys_sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
    if (!current_process) {
        return -ESRCH;  // No such process
    }
    
    // TODO: Implementar cambio de máscara de señales
    // Por ahora, retornar error
    return -ENOSYS;  // Function not implemented
}

int64_t sys_sigsuspend(const sigset_t *mask)
{
    if (!current_process) {
        return -ESRCH;  // No such process
    }
    
    // TODO: Implementar suspensión hasta señal
    // Por ahora, retornar error
    return -ENOSYS;  // Function not implemented
}
