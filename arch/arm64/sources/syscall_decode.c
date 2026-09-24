/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: syscall_decode.c
 * Description: Linux AArch64 syscall-number decoder backend.
 */

#include <ir0/syscall_id.h>

enum ir0_syscall_id syscall_decode_number(uint64_t abi_number)
{
	switch (abi_number)
	{
	case 17: return IR0_SYSCALL_GETCWD;
	case 23: return IR0_SYSCALL_DUP;
	case 24: return IR0_SYSCALL_DUP3;
	case 25: return IR0_SYSCALL_FCNTL;
	case 29: return IR0_SYSCALL_IOCTL;
	case 48: return IR0_SYSCALL_FACCESSAT;
	case 49: return IR0_SYSCALL_CHDIR;
	case 56: return IR0_SYSCALL_OPENAT;
	case 57: return IR0_SYSCALL_CLOSE;
	case 59: return IR0_SYSCALL_PIPE2;
	case 61: return IR0_SYSCALL_GETDENTS64;
	case 63: return IR0_SYSCALL_READ;
	case 64: return IR0_SYSCALL_WRITE;
	case 73: return IR0_SYSCALL_PPOLL;
	case 79: return IR0_SYSCALL_NEWFSTATAT;
	case 80: return IR0_SYSCALL_FSTAT;
	case 93: return IR0_SYSCALL_EXIT;
	case 94: return IR0_SYSCALL_EXIT_GROUP;
	case 96: return IR0_SYSCALL_SET_TID_ADDRESS;
	case 99: return IR0_SYSCALL_SET_ROBUST_LIST;
	case 101: return IR0_SYSCALL_NANOSLEEP;
	case 113: return IR0_SYSCALL_CLOCK_GETTIME;
	case 114: return IR0_SYSCALL_CLOCK_GETRES;
	case 115: return IR0_SYSCALL_CLOCK_NANOSLEEP;
	case 134: return IR0_SYSCALL_RT_SIGACTION;
	case 135: return IR0_SYSCALL_RT_SIGPROCMASK;
	case 160: return IR0_SYSCALL_UNAME;
	case 167: return IR0_SYSCALL_PRCTL;
	case 169: return IR0_SYSCALL_GETTIMEOFDAY;
	case 172: return IR0_SYSCALL_GETPID;
	case 173: return IR0_SYSCALL_GETPPID;
	case 174: return IR0_SYSCALL_GETUID;
	case 175: return IR0_SYSCALL_GETEUID;
	case 176: return IR0_SYSCALL_GETGID;
	case 177: return IR0_SYSCALL_GETEGID;
	case 178: return IR0_SYSCALL_GETTID;
	case 214: return IR0_SYSCALL_BRK;
	case 215: return IR0_SYSCALL_MUNMAP;
	case 220: return IR0_SYSCALL_CLONE;
	case 221: return IR0_SYSCALL_EXECVE;
	case 222: return IR0_SYSCALL_MMAP;
	case 226: return IR0_SYSCALL_MPROTECT;
	case 260: return IR0_SYSCALL_WAIT4;
	case 261: return IR0_SYSCALL_PRLIMIT64;
	case 278: return IR0_SYSCALL_GETRANDOM;
	case 293: return IR0_SYSCALL_RSEQ;
	default: return IR0_SYSCALL_UNKNOWN;
	}
}
