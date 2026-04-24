/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: syscall.h
 * Description: IR0 kernel source/header file
 */

#pragma once

#ifndef _IR0_SYSCALL_H
#define _IR0_SYSCALL_H

#include <stdint.h>
#include <stddef.h>
#include <ir0/stat.h>
#include <ir0/types.h>
#include <ir0/fcntl.h>
#include <ir0/poll.h>
#include <ir0/time.h>
#include <string.h>

/**
 * POSIX-compliant System Call Numbers (Linux/musl ABI)
 *
 * Numbers follow Linux x86-64 syscall conventions for musl compatibility.
 * See <ir0/bits/syscall_linux.h> for __NR_* definitions.
 */
#include <ir0/bits/syscall_linux.h>

/* Map SYS_* to Linux __NR_* for backward compatibility */
#define SYS_READ         __NR_read
#define SYS_WRITE        __NR_write
#define SYS_OPEN         __NR_open
#define SYS_CLOSE        __NR_close
#define SYS_STAT         __NR_stat
#define SYS_FSTAT        __NR_fstat
#define SYS_LSTAT        __NR_lstat
#define SYS_POLL         __NR_poll
#define SYS_LSEEK        __NR_lseek
#define SYS_MMAP         __NR_mmap
#define SYS_MPROTECT     __NR_mprotect
#define SYS_MUNMAP       __NR_munmap
#define SYS_BRK          __NR_brk
#define SYS_SIGACTION    __NR_rt_sigaction
#define SYS_SIGRETURN    __NR_rt_sigreturn
#define SYS_IOCTL        __NR_ioctl
#define SYS_PIPE         __NR_pipe
#define SYS_DUP2         __NR_dup2
#define SYS_NANOSLEEP    __NR_nanosleep
#define SYS_GETPID       __NR_getpid
#define SYS_GETUID       __NR_getuid
#define SYS_GETEUID      __NR_geteuid
#define SYS_GETGID       __NR_getgid
#define SYS_GETEGID      __NR_getegid
#define SYS_SETUID       __NR_setuid
#define SYS_SETGID       __NR_setgid
#define SYS_UMASK        __NR_umask
#define SYS_EXEC         __NR_execve
#define SYS_EXIT         __NR_exit
#define SYS_WAITPID      __NR_wait4
#define SYS_KILL         __NR_kill
#define SYS_GETCWD       __NR_getcwd
#define SYS_CHDIR        __NR_chdir
#define SYS_MKDIR        __NR_mkdir
#define SYS_RMDIR        __NR_rmdir
#define SYS_LINK         __NR_link
#define SYS_UNLINK       __NR_unlink
#define SYS_RENAME       __NR_rename
#define SYS_ACCESS       __NR_access
#define SYS_DUP          __NR_dup
#define SYS_UNAME        __NR_uname
#define SYS_CHMOD        __NR_chmod
#define SYS_CHOWN        __NR_chown
#define SYS_GETTIMEOFDAY __NR_gettimeofday
#define SYS_GETPPID      __NR_getppid
#define SYS_GETDENTS     __NR_getdents
#define SYS_MOUNT        __NR_mount
#define SYS_FORK         __NR_fork
#define SYS_CONSOLE_SCROLL __NR_console_scroll
#define SYS_CONSOLE_CLEAR __NR_console_clear
#define SYS_KEYMAP_SET   __NR_keymap_set
#define SYS_KEYMAP_GET   __NR_keymap_get
#define SYS_SUDO_AUTH    __NR_sudo_auth

/* creat: use open(O_CREAT|O_WRONLY|O_TRUNC, mode) - no separate Linux syscall */
#define SYS_CREAT        __NR_open

typedef int syscall_num_t;

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
#define DEV_FB0            "/dev/fb0"
#define DEV_EVENTS0        "/dev/events0"

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

/**
 * ir0_execve - Execute program with argv/envp (POSIX execve)
 * For Doom: argv = {"doom", "doom1.wad", NULL}, envp = {NULL}
 */
static inline int64_t ir0_execve(const char *path, char *const argv[], char *const envp[])
{
    return syscall3(SYS_EXEC, (int64_t)path, (int64_t)argv, (int64_t)envp);
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

/* creat: deprecated POSIX; use open(O_CREAT|O_WRONLY|O_TRUNC, mode) */
static inline int64_t ir0_creat(const char *pathname, mode_t mode)
{
    return syscall3(SYS_OPEN, (int64_t)pathname, O_CREAT | O_WRONLY | O_TRUNC, mode);
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

static inline int64_t ir0_ioctl(int fd, uint64_t request, void *arg)
{
    return syscall3(SYS_IOCTL, fd, request, (int64_t)arg);
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

static inline int64_t ir0_getuid(void)
{
    return syscall0(SYS_GETUID);
}

static inline int64_t ir0_geteuid(void)
{
    return syscall0(SYS_GETEUID);
}

static inline int64_t ir0_getgid(void)
{
    return syscall0(SYS_GETGID);
}

static inline int64_t ir0_getegid(void)
{
    return syscall0(SYS_GETEGID);
}

static inline int64_t ir0_setuid(uid_t uid)
{
    return syscall1(SYS_SETUID, (int64_t)uid);
}

static inline int64_t ir0_setgid(gid_t gid)
{
    return syscall1(SYS_SETGID, (int64_t)gid);
}

static inline int64_t ir0_umask(mode_t mask)
{
    return syscall1(SYS_UMASK, (int64_t)mask);
}

static inline int64_t ir0_sudo_auth(const char *password)
{
    return syscall1(SYS_SUDO_AUTH, (int64_t)password);
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

static inline int64_t ir0_rename(const char *oldpath, const char *newpath)
{
    return syscall2(SYS_RENAME, (int64_t)oldpath, (int64_t)newpath);
}

static inline int64_t ir0_access(const char *pathname, int mode)
{
    return syscall2(SYS_ACCESS, (int64_t)pathname, mode);
}

static inline int64_t ir0_dup(int oldfd)
{
    return syscall1(SYS_DUP, oldfd);
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

/* Directory creation (POSIX mkdir - use SYS_MKDIR directly, like OSDev) */
static inline int64_t ir0_mkdir(const char *path, mode_t mode)
{
    return syscall2(SYS_MKDIR, (int64_t)path, (int64_t)mode);
}

/* Directory removal (POSIX rmdir - removes empty dir only) */
static inline int64_t ir0_rmdir(const char *path)
{
    return syscall1(SYS_RMDIR, (int64_t)path);
}

/* poll - Wait for events on file descriptors */
static inline int64_t ir0_poll(struct pollfd *fds, unsigned int nfds, int timeout_ms)
{
    return syscall3(SYS_POLL, (int64_t)fds, (int64_t)nfds, (int64_t)timeout_ms);
}

/* nanosleep - Sleep for specified time (POSIX, OSDev Time And Date) */
static inline int64_t ir0_nanosleep(const struct timespec *req, struct timespec *rem)
{
    return syscall2(SYS_NANOSLEEP, (int64_t)req, (int64_t)rem);
}

/* gettimeofday - Get current time (POSIX, OSDev Time And Date) */
static inline int64_t ir0_gettimeofday(struct timeval *tv, void *tz)
{
    return syscall2(SYS_GETTIMEOFDAY, (int64_t)tv, (int64_t)tz);
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

/* Kernel log buffer (boot logs, dmesg) via /proc */
static inline int64_t ir0_dmesg(void)
{
    return ir0_open("/proc/kmsg", O_RDONLY, 0);
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
    
    /* Parse PPid from raw /proc/self/status: name\tstate\tpid\tppid\tuid\tgid */
    char *p = buf;
    int field = 0;
    while (*p && field < 4)
    {
        if (field == 3)
            return atoi(p);
        if (*p == '\t') field++;
        p++;
    }
    return -1;
}

#endif /* _IR0_SYSCALL_H */
