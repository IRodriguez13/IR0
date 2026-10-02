/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: syscall_dispatch.c
 * Description: syscall table + dispatcher (ARCH-1 split)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "syscall_dispatch.h"
#include <ir0/ktm/user_canary.h>
#include <config.h>
#include "process.h"
#include <kernel/syscalls.h>
#include "fs_syscalls.h"
#include "fs_path_syscalls.h"
#include "mm_syscalls.h"
#include "socket_syscalls.h"
#include "process_syscalls.h"
#include "io_syscalls.h"
#include "time_syscalls.h"
#include "epoll_syscalls.h"
#include <ir0/syscall_id.h>
#include <ir0/syscall_table.h>
#include <ir0/kexec.h>
#include <ir0/signals.h>
#include <ir0/futex.h>
#include <ir0/sock_udp.h>
#include <ir0/clone.h>
#include <ir0/utsname.h>
#include <ir0/console_backend.h>
#include <ir0/input_backend.h>
#include <ir0/serial_io.h>
#include <ir0/errno.h>
#include <ir0/process.h>
#include <ir0/sched.h>
#include <ir0/abi/mmap_contract.h>
#include <ir0/arch_port.h>
#include <ir0/cpu.h>
#include <ir0/context.h>
#include <ir0/sysv_shm.h>
#include <ir0/memfd.h>
#include <ir0/eventfd.h>
#include <ir0/timerfd.h>
#include <ir0/time.h>
#include <ktm.h>
#include <ktm_probe_diag.h>
#include <d1_12_read_diag.h>
#include <config.h>
#include <stddef.h>
#include <stdint.h>

/* Wrappers to adapt IR0 handlers to uniform 6-arg signature */
#define WRAP0(h) \
  static int64_t wrap_##h(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) { \
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return h(); }
#define WRAP1(h, cast1) \
  static int64_t wrap_##h(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) { \
    (void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return h((cast1)a1); }
#define WRAP2(h, c1, c2) \
  static int64_t wrap_##h(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) { \
    (void)a3;(void)a4;(void)a5;(void)a6; return h((c1)a1, (c2)a2); }
#define WRAP3(h, c1, c2, c3) \
  static int64_t wrap_##h(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) { \
    (void)a4;(void)a5;(void)a6; return h((c1)a1, (c2)a2, (c3)a3); }
#define WRAP4(h, c1, c2, c3, c4) \
  static int64_t wrap_##h(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) { \
    (void)a5;(void)a6; return h((c1)a1, (c2)a2, (c3)a3, (c4)a4); }
#define WRAP5(h, c1, c2, c3, c4, c5) \
  static int64_t wrap_##h(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) { \
    (void)a6; return h((c1)a1, (c2)a2, (c3)a3, (c4)a4, (c5)a5); }
#define WRAP6(h, c1, c2, c3, c4, c5, c6) \
  static int64_t wrap_##h(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) { \
    return (int64_t)h((c1)a1, (c2)a2, (c3)a3, (c4)a4, (c5)a5, (c6)a6); }

static int64_t wrap_sys_mmap(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
			     uint64_t a5, uint64_t a6)
{
	uintptr_t ret;

	ret = (uintptr_t)sys_mmap((void *)a1, (size_t)a2, (int)a3, (int)a4,
				  (int)a5, (off_t)a6);
	return ir0_mmap_syscall_ret(ret);
}

WRAP3(sys_socket, int, int, int)
WRAP3(sys_bind, int, const struct sockaddr *, socklen_t)
WRAP3(sys_connect, int, const struct sockaddr *, socklen_t)
WRAP2(sys_listen, int, int)
WRAP3(sys_accept, int, struct sockaddr *, socklen_t *)
WRAP4(sys_accept4, int, struct sockaddr *, socklen_t *, int)
WRAP4(sys_socketpair, int, int, int, int *)
WRAP3(sys_sendmsg, int, const struct msghdr *, int)
WRAP3(sys_recvmsg, int, struct msghdr *, int)
WRAP2(sys_shutdown, int, int)
WRAP3(sys_getsockname, int, struct sockaddr *, socklen_t *)
WRAP3(sys_getpeername, int, struct sockaddr *, socklen_t *)
WRAP5(sys_setsockopt, int, int, int, const void *, socklen_t)
WRAP5(sys_getsockopt, int, int, int, void *, socklen_t *)
WRAP3(sys_shmget, int, size_t, int)
WRAP3(sys_shmat, int, const void *, int)
WRAP1(sys_shmdt, const void *)
WRAP3(sys_shmctl, int, int, void *)
WRAP2(sys_memfd_create, const char *, unsigned int)
WRAP2(sys_eventfd2, unsigned int, int)
WRAP2(sys_timerfd_create, int, int)
WRAP4(sys_timerfd_settime, int, int, const struct itimerspec *, struct itimerspec *)
WRAP2(sys_timerfd_gettime, int, struct itimerspec *)
WRAP6(sys_sendto, int, const void *, size_t, int, const struct sockaddr *, socklen_t)
WRAP6(sys_recvfrom, int, void *, size_t, int, struct sockaddr *, socklen_t *)

WRAP1(sys_exit, int)
WRAP1(sys_exit_group, int)
WRAP3(sys_read, int, void *, size_t)
WRAP3(sys_write, int, const void *, size_t)
WRAP3(sys_readv, int, const struct iovec *, int)
WRAP3(sys_writev, int, const struct iovec *, int)
WRAP3(sys_open, const char *, int, mode_t)
WRAP1(sys_close, int)
WRAP3(sys_waitpid, pid_t, int *, int)
WRAP2(sys_link, const char *, const char *)
WRAP3(sys_readlink, const char *, char *, size_t)
WRAP4(sys_readlinkat, int, const char *, char *, size_t)
WRAP2(sys_symlink, const char *, const char *)
WRAP3(sys_symlinkat, const char *, int, const char *)
WRAP2(sys_rename, const char *, const char *)
WRAP2(sys_truncate, const char *, off_t)
WRAP2(sys_ftruncate, int, off_t)
WRAP1(sys_unlink, const char *)
WRAP3(sys_unlinkat, int, const char *, int)
WRAP4(sys_renameat, int, const char *, int, const char *)
WRAP1(sys_uname, struct utsname *)
WRAP1(sys_sysinfo, void *)
WRAP2(sys_access, const char *, int)
WRAP4(sys_faccessat, int, const char *, int, int)
WRAP1(sys_dup, int)
WRAP3(sys_dup3, int, int, int)
WRAP3(sys_exec, const char *, char *const *, char *const *)
WRAP5(sys_execveat, int, const char *, char *const *, char *const *, int)
WRAP1(sys_chdir, const char *)
WRAP1(sys_chroot, const char *)
WRAP1(sys_fchdir, int)
WRAP5(sys_mount, const char *, const char *, const char *, unsigned long,
      const void *)
WRAP2(sys_umount, const char *, int)
WRAP2(sys_mkdir, const char *, mode_t)
WRAP3(sys_mkdirat, int, const char *, mode_t)
WRAP1(sys_rmdir, const char *)
WRAP2(sys_chmod, const char *, mode_t)
WRAP3(sys_chown, const char *, uid_t, gid_t)
WRAP3(sys_lseek, int, off_t, int)
WRAP2(sys_getcwd, char *, size_t)
WRAP4(sys_utimensat, int, const char *, const struct timespec *, int)
WRAP2(sys_stat, const char *, stat_t *)
WRAP2(sys_fstat, int, stat_t *)
WRAP2(sys_statfs, const char *, void *)
WRAP2(sys_fstatfs, int, void *)
WRAP2(sys_dup2, int, int)
WRAP2(sys_flock, int, int)
WRAP2(sys_fchmod, int, mode_t)
WRAP3(sys_fchown, int, uid_t, gid_t)
WRAP4(sys_fchmodat, int, const char *, mode_t, int)
WRAP5(sys_fchownat, int, const char *, uid_t, gid_t, int)
WRAP4(sys_mknodat, int, const char *, unsigned int, unsigned int)
WRAP3(sys_mknod, const char *, unsigned int, unsigned int)
WRAP1(sys_brk, void *)
WRAP2(sys_munmap, void *, size_t)
WRAP3(sys_mprotect, void *, size_t, int)
WRAP2(sys_kill, pid_t, int)
WRAP4(sys_rt_sigaction, int, const struct sigaction *, struct sigaction *, size_t)
WRAP4(sys_rt_sigprocmask, int, const sigset_t *, sigset_t *, size_t)
WRAP2(sys_rt_sigsuspend, const sigset_t *, size_t)
WRAP2(sys_getgroups, int, gid_t *)
WRAP2(sys_setgroups, size_t, const gid_t *)
WRAP2(sys_setreuid, uid_t, uid_t)
WRAP2(sys_setregid, gid_t, gid_t)
WRAP3(sys_setresuid, uid_t, uid_t, uid_t)
WRAP3(sys_getresuid, uid_t *, uid_t *, uid_t *)
WRAP3(sys_setresgid, gid_t, gid_t, gid_t)
WRAP3(sys_getresgid, gid_t *, gid_t *, gid_t *)
WRAP2(sys_tkill, pid_t, int)
WRAP3(sys_tgkill, pid_t, pid_t, int)
WRAP2(sys_arch_prctl, int, unsigned long)
WRAP1(sys_set_tid_address, int *)
WRAP3(sys_fcntl, int, int, unsigned long)
WRAP4(sys_openat, int, const char *, int, mode_t)
WRAP4(sys_newfstatat, int, const char *, stat_t *, int)
WRAP2(sys_clock_gettime, int, struct timespec *)
WRAP6(sys_futex, int *, int, int, const struct timespec *, int *, int)
WRAP3(sys_getrandom, void *, size_t, unsigned int)
WRAP2(sys_set_robust_list, struct robust_list_head *, size_t)
WRAP3(sys_get_robust_list, int, struct robust_list_head **, size_t *)
WRAP4(sys_prlimit64, pid_t, unsigned int, const void *, void *)
WRAP2(sys_getrlimit, unsigned int, void *)
WRAP4(sys_reboot, int, int, unsigned int, void *)
WRAP4(sys_kexec_load, unsigned long, unsigned long, struct kexec_segment *, unsigned long)
WRAP2(sys_pipe2, int *, int)
WRAP1(sys_pipe, int *)
WRAP1(sys_sigreturn, struct sigcontext *)
WRAP3(sys_ioctl, int, uint64_t, void *)
WRAP3(sys_syslog, int, char *, int)
WRAP3(sys_getdents, int, void *, size_t)
WRAP3(sys_getdents64, int, void *, size_t)
WRAP3(sys_poll, struct pollfd *, unsigned int, int)
WRAP5(sys_ppoll, struct pollfd *, unsigned int, const struct timespec *,
      const sigset_t *, size_t)
WRAP5(sys_select, int, fd_set *, fd_set *, fd_set *, struct timeval *)
WRAP1(sys_epoll_create1, int)
WRAP4(sys_epoll_ctl, int, int, int, struct epoll_event *)
WRAP4(sys_epoll_wait, int, struct epoll_event *, int, int)
WRAP6(sys_epoll_pwait, int, struct epoll_event *, int, int, const void *, size_t)
WRAP6(sys_pselect6, int, fd_set *, fd_set *, fd_set *, const struct timespec *, const void *)
WRAP2(sys_nanosleep, const struct timespec *, struct timespec *)
WRAP0(sys_pause)
WRAP0(sys_vfork)
WRAP1(sys_personality, unsigned long)
WRAP2(sys_getpriority, int, int)
WRAP3(sys_setpriority, int, int, int)
WRAP0(sys_sync)
WRAP1(sys_fsync, int)
WRAP1(sys_fdatasync, int)
WRAP2(sys_gettimeofday, struct timeval *, void *)
WRAP2(sys_getitimer, int, struct itimerval *)
WRAP3(sys_setitimer, int, const struct itimerval *, struct itimerval *)
WRAP1(sys_alarm, unsigned int)
WRAP5(sys_prctl, int, unsigned long, unsigned long, unsigned long, unsigned long)
WRAP1(sys_setuid, uid_t)
WRAP1(sys_setgid, gid_t)
WRAP1(sys_umask, mode_t)
WRAP5(sys_clone, unsigned long, void *, int *, int *, unsigned long)

#undef WRAP1
#undef WRAP2
#undef WRAP3
#undef WRAP4
#undef WRAP5
#undef WRAP6

/* WRAP0 for no-arg handlers */
static int64_t wrap_sys_fork(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_fork(); }
static int64_t wrap_sys_getpid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_getpid(); }
static int64_t wrap_sys_gettid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_gettid(); }
static int64_t wrap_sys_getppid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_getppid(); }
static int64_t wrap_sys_setsid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_setsid(); }
static int64_t wrap_sys_setpgid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a3;(void)a4;(void)a5;(void)a6; return sys_setpgid((pid_t)a1, (pid_t)a2); }
static int64_t wrap_sys_getsid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_getsid((pid_t)a1); }
static int64_t wrap_sys_getpgid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_getpgid((pid_t)a1); }
static int64_t wrap_sys_getuid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_getuid(); }
static int64_t wrap_sys_geteuid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_geteuid(); }
static int64_t wrap_sys_getgid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_getgid(); }
static int64_t wrap_sys_getegid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_getegid(); }

/* Console scroll: IR0 custom syscall */
static int64_t wrap_console_scroll(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a2;(void)a3;(void)a4;(void)a5;(void)a6;
  console_backend_scroll((int)a1);
  return 0;
}

/* Console clear: IR0 custom syscall */
static int64_t wrap_console_clear(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a2;(void)a3;(void)a4;(void)a5;(void)a6;
  console_backend_clear((uint8_t)a1);
  return 0;
}

/* Keyboard layout set/get: IR0 custom syscalls */
static int64_t wrap_keymap_set(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a2;(void)a3;(void)a4;(void)a5;(void)a6;
  return input_kbd_set_layout((int)a1);
}

static int64_t wrap_keymap_get(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6;
  return input_kbd_get_layout();
}

/* ABI numbers never index common kernel state. */
static struct syscall_handler_table native_syscall_handlers;
#define syscall_semantic_table native_syscall_handlers.handlers

void syscall_table_init(void)
{
  syscall_handlers_init(&native_syscall_handlers);

  syscall_semantic_table[IR0_SYSCALL_ACCESS] = wrap_sys_access;
  syscall_semantic_table[IR0_SYSCALL_ALARM] = wrap_sys_alarm;
  syscall_semantic_table[IR0_SYSCALL_ARCH_PRCTL] = wrap_sys_arch_prctl;
  syscall_semantic_table[IR0_SYSCALL_BRK] = wrap_sys_brk;
  syscall_semantic_table[IR0_SYSCALL_CHDIR] = wrap_sys_chdir;
  syscall_semantic_table[IR0_SYSCALL_CHMOD] = wrap_sys_chmod;
  syscall_semantic_table[IR0_SYSCALL_CHOWN] = wrap_sys_chown;
  syscall_semantic_table[IR0_SYSCALL_CHROOT] = wrap_sys_chroot;
  syscall_semantic_table[IR0_SYSCALL_CLOCK_GETTIME] = wrap_sys_clock_gettime;
  syscall_semantic_table[IR0_SYSCALL_CLOCK_GETTIME64] = wrap_sys_clock_gettime;
  syscall_semantic_table[IR0_SYSCALL_CLONE] = wrap_sys_clone;
  syscall_semantic_table[IR0_SYSCALL_CLOSE] = wrap_sys_close;
  syscall_semantic_table[IR0_SYSCALL_EXTENSION_CONSOLE_CLEAR] = wrap_console_clear;
  syscall_semantic_table[IR0_SYSCALL_EXTENSION_CONSOLE_SCROLL] = wrap_console_scroll;
  syscall_semantic_table[IR0_SYSCALL_DUP] = wrap_sys_dup;
  syscall_semantic_table[IR0_SYSCALL_DUP2] = wrap_sys_dup2;
  syscall_semantic_table[IR0_SYSCALL_DUP3] = wrap_sys_dup3;
  syscall_semantic_table[IR0_SYSCALL_EPOLL_CREATE] = wrap_sys_epoll_create1;
  syscall_semantic_table[IR0_SYSCALL_EPOLL_CREATE1] = wrap_sys_epoll_create1;
  syscall_semantic_table[IR0_SYSCALL_EPOLL_CTL] = wrap_sys_epoll_ctl;
  syscall_semantic_table[IR0_SYSCALL_EPOLL_PWAIT] = wrap_sys_epoll_pwait;
  syscall_semantic_table[IR0_SYSCALL_EPOLL_WAIT] = wrap_sys_epoll_wait;
  syscall_semantic_table[IR0_SYSCALL_EVENTFD2] = wrap_sys_eventfd2;
  syscall_semantic_table[IR0_SYSCALL_EXECVE] = wrap_sys_exec;
  syscall_semantic_table[IR0_SYSCALL_EXECVEAT] = wrap_sys_execveat;
  syscall_semantic_table[IR0_SYSCALL_EXIT] = wrap_sys_exit;
  syscall_semantic_table[IR0_SYSCALL_EXIT_GROUP] = wrap_sys_exit_group;
  syscall_semantic_table[IR0_SYSCALL_FACCESSAT] = wrap_sys_faccessat;
  syscall_semantic_table[IR0_SYSCALL_FCHDIR] = wrap_sys_fchdir;
  syscall_semantic_table[IR0_SYSCALL_FCHMOD] = wrap_sys_fchmod;
  syscall_semantic_table[IR0_SYSCALL_FCHMODAT] = wrap_sys_fchmodat;
  syscall_semantic_table[IR0_SYSCALL_FCHOWN] = wrap_sys_fchown;
  syscall_semantic_table[IR0_SYSCALL_FCHOWNAT] = wrap_sys_fchownat;
  syscall_semantic_table[IR0_SYSCALL_FCNTL] = wrap_sys_fcntl;
  syscall_semantic_table[IR0_SYSCALL_FDATASYNC] = wrap_sys_fdatasync;
  syscall_semantic_table[IR0_SYSCALL_FLOCK] = wrap_sys_flock;
  syscall_semantic_table[IR0_SYSCALL_FORK] = wrap_sys_fork;
  syscall_semantic_table[IR0_SYSCALL_FSTAT] = wrap_sys_fstat;
  syscall_semantic_table[IR0_SYSCALL_FSTATFS] = wrap_sys_fstatfs;
  syscall_semantic_table[IR0_SYSCALL_FSYNC] = wrap_sys_fsync;
  syscall_semantic_table[IR0_SYSCALL_FTRUNCATE] = wrap_sys_ftruncate;
  syscall_semantic_table[IR0_SYSCALL_FUTEX] = wrap_sys_futex;
  syscall_semantic_table[IR0_SYSCALL_GET_ROBUST_LIST] = wrap_sys_get_robust_list;
  syscall_semantic_table[IR0_SYSCALL_GETCWD] = wrap_sys_getcwd;
  syscall_semantic_table[IR0_SYSCALL_GETDENTS] = wrap_sys_getdents;
  syscall_semantic_table[IR0_SYSCALL_GETDENTS64] = wrap_sys_getdents64;
  syscall_semantic_table[IR0_SYSCALL_GETEGID] = wrap_sys_getegid;
  syscall_semantic_table[IR0_SYSCALL_GETEUID] = wrap_sys_geteuid;
  syscall_semantic_table[IR0_SYSCALL_GETGID] = wrap_sys_getgid;
  syscall_semantic_table[IR0_SYSCALL_GETGROUPS] = wrap_sys_getgroups;
  syscall_semantic_table[IR0_SYSCALL_GETITIMER] = wrap_sys_getitimer;
  syscall_semantic_table[IR0_SYSCALL_GETPGID] = wrap_sys_getpgid;
  syscall_semantic_table[IR0_SYSCALL_GETPID] = wrap_sys_getpid;
  syscall_semantic_table[IR0_SYSCALL_GETPPID] = wrap_sys_getppid;
  syscall_semantic_table[IR0_SYSCALL_GETPRIORITY] = wrap_sys_getpriority;
  syscall_semantic_table[IR0_SYSCALL_GETRANDOM] = wrap_sys_getrandom;
  syscall_semantic_table[IR0_SYSCALL_GETRESGID] = wrap_sys_getresgid;
  syscall_semantic_table[IR0_SYSCALL_GETRESUID] = wrap_sys_getresuid;
  syscall_semantic_table[IR0_SYSCALL_GETRLIMIT] = wrap_sys_getrlimit;
  syscall_semantic_table[IR0_SYSCALL_GETSID] = wrap_sys_getsid;
  syscall_semantic_table[IR0_SYSCALL_GETTID] = wrap_sys_gettid;
  syscall_semantic_table[IR0_SYSCALL_GETTIMEOFDAY] = wrap_sys_gettimeofday;
  syscall_semantic_table[IR0_SYSCALL_GETUID] = wrap_sys_getuid;
  syscall_semantic_table[IR0_SYSCALL_IOCTL] = wrap_sys_ioctl;
  syscall_semantic_table[IR0_SYSCALL_KEXEC_LOAD] = wrap_sys_kexec_load;
  syscall_semantic_table[IR0_SYSCALL_EXTENSION_KEYMAP_GET] = wrap_keymap_get;
  syscall_semantic_table[IR0_SYSCALL_EXTENSION_KEYMAP_SET] = wrap_keymap_set;
  syscall_semantic_table[IR0_SYSCALL_KILL] = wrap_sys_kill;
  syscall_semantic_table[IR0_SYSCALL_LINK] = wrap_sys_link;
  syscall_semantic_table[IR0_SYSCALL_LSEEK] = wrap_sys_lseek;
  syscall_semantic_table[IR0_SYSCALL_LSTAT] = wrap_sys_stat;
  syscall_semantic_table[IR0_SYSCALL_MEMFD_CREATE] = wrap_sys_memfd_create;
  syscall_semantic_table[IR0_SYSCALL_MKDIR] = wrap_sys_mkdir;
  syscall_semantic_table[IR0_SYSCALL_MKDIRAT] = wrap_sys_mkdirat;
  syscall_semantic_table[IR0_SYSCALL_MKNOD] = wrap_sys_mknod;
  syscall_semantic_table[IR0_SYSCALL_MKNODAT] = wrap_sys_mknodat;
  syscall_semantic_table[IR0_SYSCALL_MMAP] = wrap_sys_mmap;
  syscall_semantic_table[IR0_SYSCALL_MOUNT] = wrap_sys_mount;
  syscall_semantic_table[IR0_SYSCALL_MPROTECT] = wrap_sys_mprotect;
  syscall_semantic_table[IR0_SYSCALL_MUNMAP] = wrap_sys_munmap;
  syscall_semantic_table[IR0_SYSCALL_NANOSLEEP] = wrap_sys_nanosleep;
  syscall_semantic_table[IR0_SYSCALL_NEWFSTATAT] = wrap_sys_newfstatat;
  syscall_semantic_table[IR0_SYSCALL_OPEN] = wrap_sys_open;
  syscall_semantic_table[IR0_SYSCALL_OPENAT] = wrap_sys_openat;
  syscall_semantic_table[IR0_SYSCALL_PAUSE] = wrap_sys_pause;
  syscall_semantic_table[IR0_SYSCALL_PERSONALITY] = wrap_sys_personality;
  syscall_semantic_table[IR0_SYSCALL_PIPE] = wrap_sys_pipe;
  syscall_semantic_table[IR0_SYSCALL_PIPE2] = wrap_sys_pipe2;
  syscall_semantic_table[IR0_SYSCALL_POLL] = wrap_sys_poll;
  syscall_semantic_table[IR0_SYSCALL_PPOLL] = wrap_sys_ppoll;
  syscall_semantic_table[IR0_SYSCALL_PRCTL] = wrap_sys_prctl;
  syscall_semantic_table[IR0_SYSCALL_PRLIMIT64] = wrap_sys_prlimit64;
  syscall_semantic_table[IR0_SYSCALL_PSELECT6] = wrap_sys_pselect6;
  syscall_semantic_table[IR0_SYSCALL_READ] = wrap_sys_read;
  syscall_semantic_table[IR0_SYSCALL_READLINK] = wrap_sys_readlink;
  syscall_semantic_table[IR0_SYSCALL_READLINKAT] = wrap_sys_readlinkat;
  syscall_semantic_table[IR0_SYSCALL_READV] = wrap_sys_readv;
  syscall_semantic_table[IR0_SYSCALL_REBOOT] = wrap_sys_reboot;
  syscall_semantic_table[IR0_SYSCALL_RENAME] = wrap_sys_rename;
  syscall_semantic_table[IR0_SYSCALL_RENAMEAT] = wrap_sys_renameat;
  syscall_semantic_table[IR0_SYSCALL_RMDIR] = wrap_sys_rmdir;
  syscall_semantic_table[IR0_SYSCALL_RT_SIGACTION] = wrap_sys_rt_sigaction;
  syscall_semantic_table[IR0_SYSCALL_RT_SIGPROCMASK] = wrap_sys_rt_sigprocmask;
  syscall_semantic_table[IR0_SYSCALL_RT_SIGRETURN] = wrap_sys_sigreturn;
  syscall_semantic_table[IR0_SYSCALL_RT_SIGSUSPEND] = wrap_sys_rt_sigsuspend;
  syscall_semantic_table[IR0_SYSCALL_SELECT] = wrap_sys_select;
  syscall_semantic_table[IR0_SYSCALL_SET_ROBUST_LIST] = wrap_sys_set_robust_list;
  syscall_semantic_table[IR0_SYSCALL_SET_TID_ADDRESS] = wrap_sys_set_tid_address;
  syscall_semantic_table[IR0_SYSCALL_SETGID] = wrap_sys_setgid;
  syscall_semantic_table[IR0_SYSCALL_SETGROUPS] = wrap_sys_setgroups;
  syscall_semantic_table[IR0_SYSCALL_SETITIMER] = wrap_sys_setitimer;
  syscall_semantic_table[IR0_SYSCALL_SETPGID] = wrap_sys_setpgid;
  syscall_semantic_table[IR0_SYSCALL_SETPRIORITY] = wrap_sys_setpriority;
  syscall_semantic_table[IR0_SYSCALL_SETREGID] = wrap_sys_setregid;
  syscall_semantic_table[IR0_SYSCALL_SETRESGID] = wrap_sys_setresgid;
  syscall_semantic_table[IR0_SYSCALL_SETRESUID] = wrap_sys_setresuid;
  syscall_semantic_table[IR0_SYSCALL_SETREUID] = wrap_sys_setreuid;
  syscall_semantic_table[IR0_SYSCALL_SETSID] = wrap_sys_setsid;
  syscall_semantic_table[IR0_SYSCALL_SETUID] = wrap_sys_setuid;
  syscall_semantic_table[IR0_SYSCALL_STAT] = wrap_sys_stat;
  syscall_semantic_table[IR0_SYSCALL_STATFS] = wrap_sys_statfs;
  syscall_semantic_table[IR0_SYSCALL_SYMLINK] = wrap_sys_symlink;
  syscall_semantic_table[IR0_SYSCALL_SYMLINKAT] = wrap_sys_symlinkat;
  syscall_semantic_table[IR0_SYSCALL_SYNC] = wrap_sys_sync;
  syscall_semantic_table[IR0_SYSCALL_SYSINFO] = wrap_sys_sysinfo;
  syscall_semantic_table[IR0_SYSCALL_SYSLOG] = wrap_sys_syslog;
  syscall_semantic_table[IR0_SYSCALL_TGKILL] = wrap_sys_tgkill;
  syscall_semantic_table[IR0_SYSCALL_TIMERFD_CREATE] = wrap_sys_timerfd_create;
  syscall_semantic_table[IR0_SYSCALL_TIMERFD_GETTIME] = wrap_sys_timerfd_gettime;
  syscall_semantic_table[IR0_SYSCALL_TIMERFD_SETTIME] = wrap_sys_timerfd_settime;
  syscall_semantic_table[IR0_SYSCALL_TKILL] = wrap_sys_tkill;
  syscall_semantic_table[IR0_SYSCALL_TRUNCATE] = wrap_sys_truncate;
  syscall_semantic_table[IR0_SYSCALL_UMASK] = wrap_sys_umask;
  syscall_semantic_table[IR0_SYSCALL_UMOUNT2] = wrap_sys_umount;
  syscall_semantic_table[IR0_SYSCALL_UNAME] = wrap_sys_uname;
  syscall_semantic_table[IR0_SYSCALL_UNLINK] = wrap_sys_unlink;
  syscall_semantic_table[IR0_SYSCALL_UNLINKAT] = wrap_sys_unlinkat;
  syscall_semantic_table[IR0_SYSCALL_UTIMENSAT] = wrap_sys_utimensat;
  syscall_semantic_table[IR0_SYSCALL_VFORK] = wrap_sys_vfork;
  syscall_semantic_table[IR0_SYSCALL_WAIT4] = wrap_sys_waitpid;
  syscall_semantic_table[IR0_SYSCALL_WRITE] = wrap_sys_write;
  syscall_semantic_table[IR0_SYSCALL_WRITEV] = wrap_sys_writev;

#if CONFIG_ENABLE_NETWORKING
  syscall_semantic_table[IR0_SYSCALL_ACCEPT] = wrap_sys_accept;
  syscall_semantic_table[IR0_SYSCALL_ACCEPT4] = wrap_sys_accept4;
  syscall_semantic_table[IR0_SYSCALL_BIND] = wrap_sys_bind;
  syscall_semantic_table[IR0_SYSCALL_CONNECT] = wrap_sys_connect;
  syscall_semantic_table[IR0_SYSCALL_GETPEERNAME] = wrap_sys_getpeername;
  syscall_semantic_table[IR0_SYSCALL_GETSOCKNAME] = wrap_sys_getsockname;
  syscall_semantic_table[IR0_SYSCALL_GETSOCKOPT] = wrap_sys_getsockopt;
  syscall_semantic_table[IR0_SYSCALL_LISTEN] = wrap_sys_listen;
  syscall_semantic_table[IR0_SYSCALL_RECVFROM] = wrap_sys_recvfrom;
  syscall_semantic_table[IR0_SYSCALL_RECVMSG] = wrap_sys_recvmsg;
  syscall_semantic_table[IR0_SYSCALL_SENDMSG] = wrap_sys_sendmsg;
  syscall_semantic_table[IR0_SYSCALL_SENDTO] = wrap_sys_sendto;
  syscall_semantic_table[IR0_SYSCALL_SETSOCKOPT] = wrap_sys_setsockopt;
  syscall_semantic_table[IR0_SYSCALL_SHMAT] = wrap_sys_shmat;
  syscall_semantic_table[IR0_SYSCALL_SHMCTL] = wrap_sys_shmctl;
  syscall_semantic_table[IR0_SYSCALL_SHMDT] = wrap_sys_shmdt;
  syscall_semantic_table[IR0_SYSCALL_SHMGET] = wrap_sys_shmget;
  syscall_semantic_table[IR0_SYSCALL_SHUTDOWN] = wrap_sys_shutdown;
  syscall_semantic_table[IR0_SYSCALL_SOCKET] = wrap_sys_socket;
  syscall_semantic_table[IR0_SYSCALL_SOCKETPAIR] = wrap_sys_socketpair;
#endif
}

/**
 * syscall_dispatch - Dispatch system call via table (Linux/musl ABI)
 * @syscall_num: Native Linux ABI syscall number for the active ISA
 * @arg1-arg6: System call arguments captured by the ISA entry backend
 *
 * Returns: System call return value, or -ENOSYS for unknown/unimplemented
 */
int64_t syscall_dispatch(uint64_t syscall_num, uint64_t arg1, uint64_t arg2,
                         uint64_t arg3, uint64_t arg4, uint64_t arg5,
                         uint64_t arg6)
{
  int64_t r;
  enum ir0_syscall_id syscall_id;

  syscall_id = syscall_decode_number(syscall_num);

  if (current_process)
    process_capture_syscall_frame(current_process);

  if (current_process && current_process->mode == USER_MODE)
    current_process->syscall_entry_nr = (uint32_t)syscall_num;

  /*
   * Linux: longjmp from a handler skips rt_sigreturn. Drop stale kernel
   * sigframe bookkeeping when user SP has left the handler stack.
   */
  if (current_process && current_process->mode == USER_MODE &&
      syscall_id != IR0_SYSCALL_RT_SIGRETURN)
    signals_try_abandon_sigframe(current_process);

  if (current_process && current_process->mode == USER_MODE)
  {
    fork_ret_first_syscall_entry(syscall_num,
                                 process_syscall_ip(current_process),
                                 process_syscall_sp(current_process));
  }

  if (syscall_id == IR0_SYSCALL_UNKNOWN)
    return -ENOSYS;

  KTM_TRACE_SYSCALL_ENTER((uint32_t)syscall_num);
  if (current_process && current_process->mode == USER_MODE)
  {
    ktm_probe_diag_syscall_pre(syscall_num, arg1, arg2, arg3, arg4, arg5, arg6,
			       process_syscall_ip(current_process));
    if (syscall_id == IR0_SYSCALL_READ)
    {
      d1_12_read_diag_syscall_pre(current_process, (int)arg1,
				  (uintptr_t)arg2, (size_t)arg3,
				  process_syscall_ip(current_process));
    }
  }
  r = syscall_handler_invoke(&native_syscall_handlers, NULL, syscall_id,
			     arg1, arg2, arg3, arg4, arg5, arg6);
  KTM_TRACE_SYSCALL_RET((uint32_t)syscall_num, (uint32_t)r);
  /*
   * Watchdog on the way out: rate-limited internally, so this bounds how
   * long a corrupted initial stack image can go unnoticed without putting a
   * user copy on every syscall.
   */
  if (current_process && current_process->mode == USER_MODE)
    ktm_user_canary_poll(process_pgd(current_process),
			 (uint64_t)USER_STACK_TOP,
			 (uint32_t)current_process->task.pid);
  ktm_probe_diag_syscall_post(syscall_num, r);
  if (current_process && current_process->mode == USER_MODE &&
      syscall_id == IR0_SYSCALL_READ)
    d1_12_read_diag_syscall_post(current_process, r);

  /*
   * Publish the completed return value before signal delivery.  A signal
   * arriving at this exit edge interrupted userspace *after* the syscall;
   * its sigcontext must therefore preserve @r, not manufacture -EINTR.
   * Genuinely blocked syscalls use kernel_sleep_syscall_frame instead.
   */
  if (current_process && current_process->mode == USER_MODE)
    current_process->syscall_resume_rax = (uint64_t)r;

  /* Deliver only at a proven exit-to-user edge on this task's own stack. */
  if (current_process && current_process->mode == USER_MODE)
  {
    signals_prepare_user_return(current_process);
    /* The value above is capture-only; arch resume owns this field later. */
    current_process->syscall_resume_rax = 0;
  }

  if ((syscall_id == IR0_SYSCALL_FORK || syscall_id == IR0_SYSCALL_CLONE) &&
      current_process && current_process->fork_pending_child)
  {
    /*
     * Mark ring-0 before waking the child so a timer-deferred schedule
     * resumes via kernel_ret (syscall stack), not user iretq with a stale rip.
     * IRQs off first: otherwise arm leaves KERNEL_CS+user RIP while the timer
     * can still preempt and schedule the parent as next (desk #UD class).
     * vfork does not use this path: the child already ran while the parent
     * was blocked inside vfork_process().
     */
    if (current_process->mode == USER_MODE)
    {
      disable_interrupts();
      process_arm_kernel_syscall_sleep(current_process);
    }

    /*
     * Linux-style fork exit: wake child after parent retval is in rax, then
     * keep IF=0 until sysret so the timer cannot run the child first (UP).
     */
    disable_interrupts();
    process_fork_wake_pending(current_process);
  }
  else
  {
    /*
     * Signal handler armed during this syscall (e.g. SIGALRM in recvfrom):
     * sysret would ignore task.RIP and return to the insn after the syscall.
     * Iretq into the handler instead (restorer → rt_sigreturn later).
     */
    if (current_process && current_process->mode == USER_MODE &&
        process_saved_context_present(current_process) &&
        process_signal_enter_pending(current_process))
    {
      process_signal_enter_pending_clear(current_process);
      process_restore_user_task_segments(current_process);
      current_process->irq_frame_saved = 0;
      current_process->coop_resched_resume = 0;
      current_process->want_kernel_ret = 0;
      restore_user_fs_base();
      switch_to_user_task(&current_process->task);
    }

    if (current_process && current_process->mode == USER_MODE &&
        current_process->syscall_frame_fresh)
    {
      /*
       * Cooperative in-syscall reschedule for syscall-insn (musl) tasks.
       * Resume THIS task from its saved syscall_frame via a fresh iretq, never
       * via kernel_ret on the single shared global syscall stack / user_rsp_save
       * scratch: a peer task's syscall resets that stack to the top and would
       * clobber our saved pt_regs, corrupting user registers on resume.
       */
      if (sched_user_return_take_switch())
      {
        process_arm_coop_resched_resume(current_process, (uint64_t)r);
        sched_schedule_next();
        /*
         * Control only returns here when no switch actually occurred (a
         * coop-resumed task re-enters in user mode, not at this call site).
         * Disarm so a later real switch is not misrouted through the resume.
         */
        if (current_process)
        {
          current_process->irq_frame_saved = 0;
          current_process->coop_resched_resume = 0;
        }
      }
    }
    else
    {
      if (current_process && current_process->mode == USER_MODE)
        process_arm_kernel_syscall_sleep(current_process);
      sched_need_resched_user_return();
    }
  }

  /*
   * Restore user segments in task_t only after any in-syscall schedule
   * completes; asm sysret reloads DS/ES independently.
   */
  if (current_process && current_process->mode == USER_MODE)
    process_restore_user_task_segments(current_process);

  if (current_process && current_process->mode == USER_MODE)
    signal_note_syscall_return(current_process, r);

  return r;
}
