#pragma once

#include <stdint.h>
#include <stddef.h>
#include <ir0/stat.h>
#include <ir0/types.h> // Incluir types.h para off_t

// Basic types
typedef uint32_t mode_t;

// ===============================================================================
// SYSCALL NUMBERS - Must match kernel/shell.c
// ===============================================================================

#define SYS_EXIT 0
#define SYS_WRITE 1
#define SYS_READ 2
#define SYS_GETPID 3
#define SYS_GETPPID 4
#define SYS_LS 5
#define SYS_MKDIR 6
#define SYS_PS 7
#define SYS_WRITE_FILE 8
#define SYS_CAT 9
#define SYS_TOUCH 10
#define SYS_READ_FILE 14
#define SYS_RM 11
#define SYS_FORK 12
#define SYS_WAITPID 13
#define SYS_RMDIR 40
#define SYS_MALLOC_TEST 50
#define SYS_BRK 51
#define SYS_SBRK 52
#define SYS_MMAP 53
#define SYS_MUNMAP 54
#define SYS_MPROTECT 55
#define SYS_EXEC 56
#define SYS_FSTAT 57
#define SYS_STAT 58
#define SYS_OPEN 59
#define SYS_CLOSE 60
#define SYS_LS_DETAILED 61
#define SYS_CREAT 62
#define SYS_CHDIR 80
#define SYS_GETCWD 79
#define SYS_UNLINK 87
#define SYS_RMDIR_R 88
#define SYS_MOUNT 90
#define SYS_CHMOD 100 // Definición de SYS_CHMOD

// ===============================================================================
// SYSCALL WRAPPER FUNCTIONS
// ===============================================================================

// Low-level syscall interface
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

// Generic syscall (for compatibility)
static inline int64_t syscall(int64_t num, int64_t arg1, int64_t arg2, int64_t arg3)
{
    return syscall3(num, arg1, arg2, arg3);
}

// ===============================================================================
// HIGH-LEVEL SYSCALL WRAPPERS
// ===============================================================================

// Process management
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

static inline int64_t ir0_getpid(void)
{
    return syscall0(SYS_GETPID);
}

static inline int64_t ir0_waitpid(int64_t pid, int *status)
{
    return syscall2(SYS_WAITPID, pid, (int64_t)status);
}

// I/O operations
static inline int64_t ir0_write(int fd, const void *buf, size_t count)
{
    return syscall3(SYS_WRITE, fd, (int64_t)buf, count);
}

static inline int64_t ir0_read(int fd, void *buf, size_t count)
{
    return syscall3(SYS_READ, fd, (int64_t)buf, count);
}

// File operations
static inline int64_t ir0_write_file(const char *path, const char *content)
{
    return syscall2(SYS_WRITE_FILE, (int64_t)path, (int64_t)content);
}

static inline int64_t ir0_cat(const char *path)
{
    return syscall1(SYS_CAT, (int64_t)path);
}

static inline int64_t ir0_touch(const char *path)
{
    return syscall1(SYS_TOUCH, (int64_t)path);
}

static inline int64_t ir0_rm(const char *path)
{
    return syscall1(SYS_RM, (int64_t)path);
}

static inline int64_t ir0_mkdir(const char *path)
{
    return syscall2(SYS_MKDIR, (int64_t)path, 0755);
}

static inline int64_t ir0_ls(const char *path)
{
    return syscall1(SYS_LS, (int64_t)path);
}

// System info
static inline int64_t ir0_ps(void)
{
    return syscall0(SYS_PS);
}

// Memory management
static inline void *ir0_sbrk(intptr_t increment)
{
    return (void *)syscall1(SYS_SBRK, increment);
}

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

// File status
static inline int64_t ir0_fstat(int fd, stat_t *buf)
{
    return syscall2(SYS_FSTAT, fd, (int64_t)buf);
}

static inline int64_t ir0_stat(const char *pathname, stat_t *buf)
{
    return syscall2(SYS_STAT, (int64_t)pathname, (int64_t)buf);
}

// Mount filesystem: dev (e.g. "/dev/sda1"), mountpoint (e.g. "/"), fstype (e.g. "minix")
static inline int64_t ir0_mount(const char *dev, const char *mountpoint, const char *fstype)
{
    return syscall3(SYS_MOUNT, (int64_t)dev, (int64_t)mountpoint, (int64_t)fstype);
}

static inline int64_t ir0_open(const char *pathname, int flags, mode_t mode)
{
    return syscall3(SYS_OPEN, (int64_t)pathname, flags, mode);
}

static inline int64_t ir0_close(int fd)
{
    return syscall1(SYS_CLOSE, fd);
}

