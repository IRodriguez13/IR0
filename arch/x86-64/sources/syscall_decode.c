/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: syscall_decode.c
 * Description: Linux x86-64 syscall-number decoder backend.
 */

#include <ir0/syscall_id.h>
#include <ir0/syscall_linux.h>

enum ir0_syscall_id syscall_decode_number(uint64_t abi_number)
{
	switch (abi_number)
	{
	case __NR_read: return IR0_SYSCALL_READ;
	case __NR_write: return IR0_SYSCALL_WRITE;
	case __NR_openat: return IR0_SYSCALL_OPENAT;
	case __NR_close: return IR0_SYSCALL_CLOSE;
	case __NR_faccessat: return IR0_SYSCALL_FACCESSAT;
	case __NR_newfstatat: return IR0_SYSCALL_NEWFSTATAT;
	case __NR_fstat: return IR0_SYSCALL_FSTAT;
	case __NR_getdents64: return IR0_SYSCALL_GETDENTS64;
	case __NR_dup: return IR0_SYSCALL_DUP;
	case __NR_dup3: return IR0_SYSCALL_DUP3;
	case __NR_pipe2: return IR0_SYSCALL_PIPE2;
	case __NR_ioctl: return IR0_SYSCALL_IOCTL;
	case __NR_fcntl: return IR0_SYSCALL_FCNTL;
	case __NR_clone: return IR0_SYSCALL_CLONE;
	case __NR_fork: return IR0_SYSCALL_FORK;
	case __NR_execve: return IR0_SYSCALL_EXECVE;
	case __NR_wait4: return IR0_SYSCALL_WAIT4;
	case __NR_exit: return IR0_SYSCALL_EXIT;
	case __NR_exit_group: return IR0_SYSCALL_EXIT_GROUP;
	case __NR_nanosleep: return IR0_SYSCALL_NANOSLEEP;
	case __NR_clock_gettime:
	case __NR_clock_gettime64:
		return IR0_SYSCALL_CLOCK_GETTIME;
	case __NR_gettimeofday: return IR0_SYSCALL_GETTIMEOFDAY;
	case __NR_getpid: return IR0_SYSCALL_GETPID;
	case __NR_getppid: return IR0_SYSCALL_GETPPID;
	case __NR_gettid: return IR0_SYSCALL_GETTID;
	case __NR_getuid: return IR0_SYSCALL_GETUID;
	case __NR_geteuid: return IR0_SYSCALL_GETEUID;
	case __NR_getgid: return IR0_SYSCALL_GETGID;
	case __NR_getegid: return IR0_SYSCALL_GETEGID;
	case __NR_set_tid_address: return IR0_SYSCALL_SET_TID_ADDRESS;
	case __NR_set_robust_list: return IR0_SYSCALL_SET_ROBUST_LIST;
	case __NR_rt_sigaction: return IR0_SYSCALL_RT_SIGACTION;
	case __NR_rt_sigprocmask: return IR0_SYSCALL_RT_SIGPROCMASK;
	case __NR_rt_sigreturn: return IR0_SYSCALL_RT_SIGRETURN;
	case __NR_uname: return IR0_SYSCALL_UNAME;
	case __NR_prctl: return IR0_SYSCALL_PRCTL;
	case __NR_getcwd: return IR0_SYSCALL_GETCWD;
	case __NR_chdir: return IR0_SYSCALL_CHDIR;
	case __NR_brk: return IR0_SYSCALL_BRK;
	case __NR_munmap: return IR0_SYSCALL_MUNMAP;
	case __NR_mmap: return IR0_SYSCALL_MMAP;
	case __NR_mprotect: return IR0_SYSCALL_MPROTECT;
	case __NR_ppoll: return IR0_SYSCALL_PPOLL;
	case __NR_prlimit64: return IR0_SYSCALL_PRLIMIT64;
	case __NR_getrandom: return IR0_SYSCALL_GETRANDOM;
	default: return IR0_SYSCALL_UNKNOWN;
	}
}
