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
	case 19: return IR0_SYSCALL_EVENTFD2;
	case 20: return IR0_SYSCALL_EPOLL_CREATE1;
	case 21: return IR0_SYSCALL_EPOLL_CTL;
	case 22: return IR0_SYSCALL_EPOLL_PWAIT;
	case 23: return IR0_SYSCALL_DUP;
	case 24: return IR0_SYSCALL_DUP3;
	case 25: return IR0_SYSCALL_FCNTL;
	case 29: return IR0_SYSCALL_IOCTL;
	case 32: return IR0_SYSCALL_FLOCK;
	case 33: return IR0_SYSCALL_MKNODAT;
	case 34: return IR0_SYSCALL_MKDIRAT;
	case 35: return IR0_SYSCALL_UNLINKAT;
	case 36: return IR0_SYSCALL_SYMLINKAT;
	case 38: return IR0_SYSCALL_RENAMEAT;
	case 39: return IR0_SYSCALL_UMOUNT2;
	case 40: return IR0_SYSCALL_MOUNT;
	case 48: return IR0_SYSCALL_FACCESSAT;
	case 49: return IR0_SYSCALL_CHDIR;
	case 50: return IR0_SYSCALL_FCHDIR;
	case 51: return IR0_SYSCALL_CHROOT;
	case 52: return IR0_SYSCALL_FCHMOD;
	case 53: return IR0_SYSCALL_FCHMODAT;
	case 54: return IR0_SYSCALL_FCHOWNAT;
	case 55: return IR0_SYSCALL_FCHOWN;
	case 56: return IR0_SYSCALL_OPENAT;
	case 57: return IR0_SYSCALL_CLOSE;
	case 59: return IR0_SYSCALL_PIPE2;
	case 61: return IR0_SYSCALL_GETDENTS64;
	case 63: return IR0_SYSCALL_READ;
	case 64: return IR0_SYSCALL_WRITE;
	case 65: return IR0_SYSCALL_READV;
	case 66: return IR0_SYSCALL_WRITEV;
	case 72: return IR0_SYSCALL_PSELECT6;
	case 73: return IR0_SYSCALL_PPOLL;
	case 78: return IR0_SYSCALL_READLINKAT;
	case 79: return IR0_SYSCALL_NEWFSTATAT;
	case 80: return IR0_SYSCALL_FSTAT;
	case 81: return IR0_SYSCALL_SYNC;
	case 82: return IR0_SYSCALL_FSYNC;
	case 83: return IR0_SYSCALL_FDATASYNC;
	case 85: return IR0_SYSCALL_TIMERFD_CREATE;
	case 86: return IR0_SYSCALL_TIMERFD_SETTIME;
	case 87: return IR0_SYSCALL_TIMERFD_GETTIME;
	case 88: return IR0_SYSCALL_UTIMENSAT;
	case 92: return IR0_SYSCALL_PERSONALITY;
	case 93: return IR0_SYSCALL_EXIT;
	case 94: return IR0_SYSCALL_EXIT_GROUP;
	case 96: return IR0_SYSCALL_SET_TID_ADDRESS;
	case 98: return IR0_SYSCALL_FUTEX;
	case 99: return IR0_SYSCALL_SET_ROBUST_LIST;
	case 100: return IR0_SYSCALL_GET_ROBUST_LIST;
	case 101: return IR0_SYSCALL_NANOSLEEP;
	case 102: return IR0_SYSCALL_GETITIMER;
	case 103: return IR0_SYSCALL_SETITIMER;
	case 104: return IR0_SYSCALL_KEXEC_LOAD;
	case 113: return IR0_SYSCALL_CLOCK_GETTIME;
	case 114: return IR0_SYSCALL_CLOCK_GETRES;
	case 115: return IR0_SYSCALL_CLOCK_NANOSLEEP;
	case 116: return IR0_SYSCALL_SYSLOG;
	case 129: return IR0_SYSCALL_KILL;
	case 130: return IR0_SYSCALL_TKILL;
	case 131: return IR0_SYSCALL_TGKILL;
	case 133: return IR0_SYSCALL_RT_SIGSUSPEND;
	case 134: return IR0_SYSCALL_RT_SIGACTION;
	case 135: return IR0_SYSCALL_RT_SIGPROCMASK;
	case 139: return IR0_SYSCALL_RT_SIGRETURN;
	case 140: return IR0_SYSCALL_SETPRIORITY;
	case 141: return IR0_SYSCALL_GETPRIORITY;
	case 142: return IR0_SYSCALL_REBOOT;
	case 143: return IR0_SYSCALL_SETREGID;
	case 144: return IR0_SYSCALL_SETGID;
	case 145: return IR0_SYSCALL_SETREUID;
	case 146: return IR0_SYSCALL_SETUID;
	case 147: return IR0_SYSCALL_SETRESUID;
	case 148: return IR0_SYSCALL_GETRESUID;
	case 149: return IR0_SYSCALL_SETRESGID;
	case 150: return IR0_SYSCALL_GETRESGID;
	case 154: return IR0_SYSCALL_SETPGID;
	case 155: return IR0_SYSCALL_GETPGID;
	case 156: return IR0_SYSCALL_GETSID;
	case 157: return IR0_SYSCALL_SETSID;
	case 158: return IR0_SYSCALL_GETGROUPS;
	case 159: return IR0_SYSCALL_SETGROUPS;
	case 160: return IR0_SYSCALL_UNAME;
	case 163: return IR0_SYSCALL_GETRLIMIT;
	case 166: return IR0_SYSCALL_UMASK;
	case 167: return IR0_SYSCALL_PRCTL;
	case 169: return IR0_SYSCALL_GETTIMEOFDAY;
	case 172: return IR0_SYSCALL_GETPID;
	case 173: return IR0_SYSCALL_GETPPID;
	case 174: return IR0_SYSCALL_GETUID;
	case 175: return IR0_SYSCALL_GETEUID;
	case 176: return IR0_SYSCALL_GETGID;
	case 177: return IR0_SYSCALL_GETEGID;
	case 178: return IR0_SYSCALL_GETTID;
	case 179: return IR0_SYSCALL_SYSINFO;
	case 194: return IR0_SYSCALL_SHMGET;
	case 195: return IR0_SYSCALL_SHMCTL;
	case 196: return IR0_SYSCALL_SHMAT;
	case 197: return IR0_SYSCALL_SHMDT;
	case 198: return IR0_SYSCALL_SOCKET;
	case 199: return IR0_SYSCALL_SOCKETPAIR;
	case 200: return IR0_SYSCALL_BIND;
	case 201: return IR0_SYSCALL_LISTEN;
	case 202: return IR0_SYSCALL_ACCEPT;
	case 203: return IR0_SYSCALL_CONNECT;
	case 204: return IR0_SYSCALL_GETSOCKNAME;
	case 205: return IR0_SYSCALL_GETPEERNAME;
	case 206: return IR0_SYSCALL_SENDTO;
	case 207: return IR0_SYSCALL_RECVFROM;
	case 208: return IR0_SYSCALL_SETSOCKOPT;
	case 209: return IR0_SYSCALL_GETSOCKOPT;
	case 210: return IR0_SYSCALL_SHUTDOWN;
	case 211: return IR0_SYSCALL_SENDMSG;
	case 212: return IR0_SYSCALL_RECVMSG;
	case 214: return IR0_SYSCALL_BRK;
	case 215: return IR0_SYSCALL_MUNMAP;
	case 220: return IR0_SYSCALL_CLONE;
	case 221: return IR0_SYSCALL_EXECVE;
	case 222: return IR0_SYSCALL_MMAP;
	case 226: return IR0_SYSCALL_MPROTECT;
	case 242: return IR0_SYSCALL_ACCEPT4;
	case 260: return IR0_SYSCALL_WAIT4;
	case 261: return IR0_SYSCALL_PRLIMIT64;
	case 278: return IR0_SYSCALL_GETRANDOM;
	case 279: return IR0_SYSCALL_MEMFD_CREATE;
	case 281: return IR0_SYSCALL_EXECVEAT;
	case 293: return IR0_SYSCALL_RSEQ;
	default: return IR0_SYSCALL_UNKNOWN;
	}
}
