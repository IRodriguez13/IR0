#pragma once

#ifndef _IR0_SYSCALL_H
#define _IR0_SYSCALL_H

#include <stdint.h>
#include <stddef.h>
#include <ir0/stat.h>
#include <ir0/types.h>
#include <ir0/fcntl.h>
#include <string.h>

/* Basic types */
typedef uint32_t mode_t;

/**
 * POSIX-compliant System Call Numbers
 * 
 * This enum contains only standard POSIX syscalls, following the
 * principle of "everything is a file" where possible. Custom operations
 * should be accessed through /proc, /dev, or /sys filesystems instead
 * of custom syscalls.
 * 
 * Numbers follow standard POSIX/Linux syscall conventions for compatibility.
 */
typedef enum {
    /* Process management - POSIX */
    SYS_EXIT = 0,
    SYS_FORK = 1,
    SYS_READ = 2,
    SYS_WRITE = 3,
    SYS_OPEN = 4,
    SYS_CLOSE = 5,
    SYS_WAITPID = 6,
    SYS_CREAT = 7,      /* POSIX but deprecated, use open(O_CREAT) */
    SYS_LINK = 8,
    SYS_UNLINK = 9,
    SYS_EXEC = 10,      /* execve in POSIX */
    SYS_CHDIR = 11,
    SYS_GETPID = 12,
    SYS_MOUNT = 13,     /* Linux-specific but essential for filesystems */
    
    /* File operations - POSIX */
    SYS_MKDIR = 14,
    SYS_RMDIR = 15,
    SYS_CHMOD = 16,
    SYS_LSEEK = 17,
    SYS_GETCWD = 18,
    SYS_STAT = 19,
    SYS_FSTAT = 20,
    SYS_DUP2 = 21,
    
    /* Memory management - POSIX */
    SYS_BRK = 22,       /* Legacy but POSIX-compliant */
    SYS_MMAP = 23,
    SYS_MUNMAP = 24,
    SYS_MPROTECT = 25,
    
    /* Process info - POSIX */
    SYS_GETPPID = 26,
    SYS_KILL = 27,      /* Signal handling - POSIX */
    SYS_SIGACTION = 28, /* Signal action - POSIX */
    SYS_PIPE = 29,      /* Pipe creation - POSIX */
    SYS_SIGRETURN = 30, /* Return from signal handler - POSIX */
} syscall_num_t;

/* Virtual filesystem paths for file operations */
#define VFS_PROC_PATH     "/proc"
#define VFS_DEV_PATH       "/dev"
#define VFS_SYS_PATH       "/sys"

/* Standard device nodes */
#define DEV_NULL           "/dev/null"
#define DEV_ZERO           "/dev/zero"
#define DEV_CONSOLE        "/dev/console"
#define DEV_TTY            "/dev/tty"
#define DEV_AUDIO          "/dev/audio"
#define DEV_MOUSE          "/dev/mouse"
#define DEV_NET            "/dev/net"
#define DEV_DISK           "/dev/disk"
#define DEV_KMSG           "/dev/kmsg"

/* Process information files in /proc */
#define PROC_STATUS        "/proc/self/status"
#define PROC_MEMINFO       "/proc/meminfo"
#define PROC_CPUINFO       "/proc/cpuinfo"
#define PROC_MOUNTS        "/proc/mounts"
#define PROC_VERSION       "/proc/version"

/* System information files in /proc (legacy /sys paths redirected) */
#define SYS_NETINFO_PATH   "/proc/netinfo"
#define SYS_DRIVERS_PATH   "/proc/drivers"
#define SYS_UPTIME_PATH    "/proc/uptime"

/* Standard file descriptors */
#define STDIN_FILENO   0
#define STDOUT_FILENO  1
#define STDERR_FILENO  2

/* Access modes for permission checking - use definitions from permissions.h */
#include <ir0/permissions.h>
#define ACCESS_EXECUTE ACCESS_EXEC

/* Memory protection flags for mmap */
#define PROT_READ     0x1
#define PROT_WRITE    0x2
#define PROT_EXEC     0x4
#define PROT_NONE     0x0

/* Mapping flags for mmap */
#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20


/* Low-level syscall interface */
static inline int64_t syscall0(int64_t num)
{
    int64_t sysret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(sysret)
        : "a"(num)
        : "memory");
    return sysret;
}

static inline int64_t syscall1(int64_t num, int64_t arg1)
{
    int64_t result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(num), "b"(arg1)
        : "memory");
    return result;
}

static inline int64_t syscall2(int64_t num, int64_t arg1, int64_t arg2)
{
    int64_t result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(num), "b"(arg1), "c"(arg2)
        : "memory");
    return result;
}

static inline int64_t syscall3(int64_t num, int64_t arg1, int64_t arg2, int64_t arg3)
{
    int64_t result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3)
        : "memory");
    return result;
}

static inline int64_t syscall6(int64_t num, int64_t arg1, int64_t arg2, int64_t arg3, int64_t arg4, int64_t arg5, int64_t arg6)
{
    int64_t result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3), "S"(arg4), "D"(arg5), "r"(arg6)
        : "memory");
    return result;
}

/* Generic syscall */
static inline int64_t syscall(int64_t num, int64_t arg1, int64_t arg2, int64_t arg3)
{
    return syscall3(num, arg1, arg2, arg3);
}

/* POSIX WRAPPER FUNCTIONS */

/* Process management */
static inline void ir0_exit(int status)
{
    syscall1(SYS_EXIT, status);
    __builtin_unreachable();
}

static inline int64_t ir0_fork(void)
{
    return syscall0(SYS_FORK);
}

static inline int64_t ir0_exec(const char *path)
{
    return syscall1(SYS_EXEC, (int64_t)path);
}

static inline int64_t ir0_waitpid(int64_t pid, int *status)
{
    return syscall2(SYS_WAITPID, pid, (int64_t)status);
}

/* Universal I/O interface */
static inline int64_t ir0_open(const char *pathname, int flags, mode_t mode)
{
    return syscall3(SYS_OPEN, (int64_t)pathname, flags, mode);
}

static inline int64_t ir0_close(int fd)
{
    return syscall1(SYS_CLOSE, fd);
}

static inline int64_t ir0_read(int fd, void *buf, size_t count)
{
    return syscall3(SYS_READ, fd, (int64_t)buf, count);
}

static inline int64_t ir0_write(int fd, const void *buf, size_t count)
{
    return syscall3(SYS_WRITE, fd, (int64_t)buf, count);
}

static inline int64_t ir0_lseek(int fd, off_t offset, int whence)
{
    return syscall3(SYS_LSEEK, fd, offset, whence);
}

/* File status */
static inline int64_t ir0_stat(const char *pathname, stat_t *buf)
{
    return syscall2(SYS_STAT, (int64_t)pathname, (int64_t)buf);
}

static inline int64_t ir0_fstat(int fd, stat_t *buf)
{
    return syscall2(SYS_FSTAT, fd, (int64_t)buf);
}

/* Memory management */
/* sbrk is implemented as a wrapper around brk in userspace libraries */
/* POSIX does not require sbrk as a syscall - it's typically a library function */

static inline int64_t ir0_brk(void *addr)
{
    return syscall1(SYS_BRK, (int64_t)addr);
}

static inline void *ir0_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    return (void *)syscall6(SYS_MMAP, (int64_t)addr, length, prot, flags, fd, offset);
}

static inline int64_t ir0_munmap(void *addr, size_t length)
{
    return syscall2(SYS_MUNMAP, (int64_t)addr, length);
}

/* Process info */
static inline int64_t ir0_getpid(void)
{
    return syscall0(SYS_GETPID);
}

/* Directory operations */
static inline int64_t ir0_getcwd(char *buf, size_t size)
{
    return syscall2(SYS_GETCWD, (int64_t)buf, size);
}

static inline int64_t ir0_chdir(const char *path)
{
    return syscall1(SYS_CHDIR, (int64_t)path);
}

/* Filesystem operations */
static inline int64_t ir0_mount(const char *dev, const char *mountpoint, const char *fstype)
{
    return syscall3(SYS_MOUNT, (int64_t)dev, (int64_t)mountpoint, (int64_t)fstype);
}

static inline int64_t ir0_unlink(const char *pathname)
{
    return syscall1(SYS_UNLINK, (int64_t)pathname);
}

static inline int64_t ir0_link(const char *oldpath, const char *newpath)
{
    return syscall2(SYS_LINK, (int64_t)oldpath, (int64_t)newpath);
}

/* Advanced I/O */
static inline int64_t ir0_dup2(int oldfd, int newfd)
{
    return syscall2(SYS_DUP2, oldfd, newfd);
}

/* Permissions */
static inline int64_t ir0_chmod(const char *pathname, mode_t mode)
{
    return syscall2(SYS_CHMOD, (int64_t)pathname, mode);
}

/* Unix-style file operations for legacy compatibility */

/* Process listing via /proc filesystem */
static inline int64_t ir0_ps(void)
{
    return ir0_open("/proc", O_RDONLY, 0);
}

/* Directory listing */
static inline int64_t ir0_ls(const char *path)
{
    return ir0_open(path, O_RDONLY | O_DIRECTORY, 0);
}

/* File creation */
static inline int64_t ir0_touch(const char *path)
{
    return ir0_open(path, O_CREAT | O_WRONLY, 0644);
}

/* File deletion */
static inline int64_t ir0_rm(const char *path)
{
    return ir0_unlink(path);
}

/* Directory creation */
static inline int64_t ir0_mkdir(const char *path, mode_t mode)
{
    return ir0_open(path, O_CREAT | O_DIRECTORY, mode);
}

/* Directory removal */
static inline int64_t ir0_rmdir(const char *path)
{
    return ir0_unlink(path);
}

/* Network information via /sys filesystem */
static inline int64_t ir0_netinfo(void)
{
    return ir0_open(SYS_NETINFO_PATH, O_RDONLY, 0);
}

/* Driver listing via /sys filesystem */
static inline int64_t ir0_lsdrv(void)
{
    return ir0_open(SYS_DRIVERS_PATH, O_RDONLY, 0);
}

/* Audio operations via /dev filesystem */
static inline int64_t ir0_audio_test(const void *data, size_t size)
{
    int fd = ir0_open("/dev/audio", O_WRONLY, 0);
    if (fd < 0) return fd;
    int64_t result = ir0_write(fd, data, size);
    ir0_close(fd);
    return result;
}

/* Mouse operations via /dev filesystem */
static inline int64_t ir0_mouse_test(void *buf, size_t size)
{
    int fd = ir0_open("/dev/mouse", O_RDONLY, 0);
    if (fd < 0) return fd;
    int64_t result = ir0_read(fd, buf, size);
    ir0_close(fd);
    return result;
}

/* Kernel messages via /dev filesystem */
static inline int64_t ir0_dmesg(void)
{
    return ir0_open("/dev/kmsg", O_RDONLY, 0);
}

/* Network operations via /dev filesystem */
static inline int64_t ir0_ping(const char *host)
{
    int fd = ir0_open("/dev/net", O_WRONLY, 0);
    if (fd < 0) return fd;
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "ping %s", host);
    int64_t result = ir0_write(fd, cmd, strlen(cmd));
    ir0_close(fd);
    return result;
}

static inline int64_t ir0_ifconfig(const char *config)
{
    int fd = ir0_open("/dev/net", O_WRONLY, 0);
    if (fd < 0) return fd;
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "ifconfig %s", config);
    int64_t result = ir0_write(fd, cmd, strlen(cmd));
    ir0_close(fd);
    return result;
}

/* Disk operations via /dev filesystem */
static inline int64_t ir0_df(void)
{
    return ir0_open("/dev/disk", O_RDONLY, 0);
}

/* Memory allocation via mmap */
static inline void *ir0_malloc_test(size_t size)
{
    return ir0_mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}

/* Parent PID via /proc filesystem */
static inline int64_t ir0_getppid(void)
{
    int fd = ir0_open("/proc/self/status", O_RDONLY, 0);
    if (fd < 0) return fd;
    
    char buf[1024];
    int64_t bytes = ir0_read(fd, buf, sizeof(buf) - 1);
    ir0_close(fd);
    
    if (bytes <= 0) return -1;
    buf[bytes] = '\0';
    
    /* Parse PPid from /proc/self/status */
    char *ppid_line = strstr(buf, "PPid:");
    if (!ppid_line) return -1;
    
    return atoi(ppid_line + 6);  /* Skip "PPid:" */
}

#endif /* _IR0_SYSCALL_H */
