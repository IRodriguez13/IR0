/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 - Linux x86-64 Syscall Numbers (musl ABI compatibility)
 *
 * Numbers from Linux arch/x86/entry/syscalls/syscall_64.tbl
 * Used for musl libc and Linux ABI compatibility.
 */
#ifndef _IR0_BITS_SYSCALL_LINUX_H
#define _IR0_BITS_SYSCALL_LINUX_H

/* Linux x86-64 syscall numbers (subset for musl/Doom) */
#define __NR_read           0
#define __NR_write          1
#define __NR_open           2
#define __NR_close          3
#define __NR_stat           4
#define __NR_fstat          5
#define __NR_lstat          6
#define __NR_poll           7
#define __NR_lseek          8
#define __NR_mmap           9
#define __NR_mprotect      10
#define __NR_munmap        11
#define __NR_brk           12
#define __NR_rt_sigaction  13
#define __NR_rt_sigprocmask 14
#define __NR_rt_sigreturn  15
#define __NR_ioctl         16
#define __NR_pread64        17
#define __NR_pwrite64      18
#define __NR_readv         19
#define __NR_writev        20
#define __NR_access        21
#define __NR_pipe          22
#define __NR_select        23
#define __NR_sched_yield   24
#define __NR_mremap        25
#define __NR_msync         26
#define __NR_mincore       27
#define __NR_madvise       28
#define __NR_dup           32
#define __NR_dup2          33
#define __NR_pause         34
#define __NR_nanosleep     35
#define __NR_getitimer     36
#define __NR_alarm         37
#define __NR_setitimer     38
#define __NR_getpid        39
#define __NR_socket        41
#define __NR_connect       42
#define __NR_accept        43
#define __NR_sendto        44
#define __NR_recvfrom      45
#define __NR_sendmsg       46
#define __NR_recvmsg       47
#define __NR_shutdown      48
#define __NR_bind          49
#define __NR_listen        50
#define __NR_getsockname   51
#define __NR_getpeername   52
#define __NR_socketpair    53
#define __NR_setsockopt    54
#define __NR_getsockopt    55
#define __NR_clone         56
#define __NR_fork          57
#define __NR_vfork         58
#define __NR_execve        59
#define __NR_exit          60
#define __NR_wait4         61
#define __NR_kill          62
#define __NR_uname         63
#define __NR_fcntl         72
#define __NR_flock         73
#define __NR_fsync         74
#define __NR_truncate      76
#define __NR_ftruncate     77
#define __NR_getdents      78
#define __NR_getcwd        79
#define __NR_chdir         80
#define __NR_fchdir        81
#define __NR_rename        82
#define __NR_mkdir         83
#define __NR_rmdir         84
#define __NR_link          86
#define __NR_unlink        87
#define __NR_symlink       88
#define __NR_readlink      89
#define __NR_chmod         90
#define __NR_fchmod        91
#define __NR_chown         92
#define __NR_fchown        93
#define __NR_lchown        94
#define __NR_umask         95
#define __NR_gettimeofday  96
#define __NR_getrlimit     97
#define __NR_getrusage     98
#define __NR_sysinfo       99
#define __NR_ptrace       101
#define __NR_getuid       102
#define __NR_syslog       103
#define __NR_getgid       104
#define __NR_setuid       105
#define __NR_setgid       106
#define __NR_geteuid      107
#define __NR_getegid      108
#define __NR_setpgid      109
#define __NR_getppid      110
#define __NR_setsid       112
#define __NR_setgroups    116
#define __NR_setresuid    117
#define __NR_getresuid    118
#define __NR_setresgid    119
#define __NR_getresgid    120
#define __NR_mount        165
#define __NR_umount2      166
#define __NR_reboot       169
#define __NR_exit_group   231

/* IR0 custom syscalls (outside Linux range) */
#define __NR_console_scroll   400
#define __NR_console_clear    401

/* Max syscall number we handle (for table size) */
#define __NR_syscall_max   450

#endif /* _IR0_BITS_SYSCALL_LINUX_H */
