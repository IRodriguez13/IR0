// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: signals.h
 * Description: Basic signal definitions for process management
 */

#ifndef _IR0_SIGNALS_H
#define _IR0_SIGNALS_H

#include <stdint.h>


/* Standard Unix signals - essential set for error handling */
/* Hardware/CPU exceptions */
#define SIGSEGV  11   /* Segmentation violation (invalid memory access) */
#define SIGFPE    8   /* Floating point exception (divide by zero, overflow) */
#define SIGILL    4   /* Illegal instruction */
#define SIGBUS    7   /* Bus error (misaligned memory access) */
#define SIGTRAP   5   /* Trace/breakpoint trap */

/* Termination signals */
#define SIGKILL   9   /* Kill process (cannot be caught or ignored) */
#define SIGTERM  15   /* Termination signal (can be caught) */
#define SIGINT    2   /* Interrupt from keyboard (Ctrl+C) */
#define SIGQUIT   3   /* Quit from keyboard (Ctrl+\\) */

/* Process control */
#define SIGCHLD  17   /* Child process terminated or stopped */
#define SIGSTOP  19   /* Stop process (cannot be caught) */
#define SIGCONT  18   /* Continue if stopped */

/* Other */
#define SIGABRT   6   /* Abort signal (from abort()) */
#define SIGALRM  14   /* Timer signal (from alarm()) */
#define SIGUSR1  10   /* User-defined signal 1 */
#define SIGUSR2  12   /* User-defined signal 2 */

/* Signal bitmask helpers */
#define SIGNAL_MASK(sig) (1U << (sig))

/* Maximum signal number */
#define _NSIG 32

/* Special signal handler values */
#define SIG_DFL ((void (*)(int))0)  /* Default handler */
#define SIG_IGN ((void (*)(int))1)  /* Ignore signal */
#define SIG_ERR ((void (*)(int))-1) /* Error return */

/**
 * struct sigaction - Signal action structure (simplified POSIX sigaction)
 */
struct sigaction {
    void (*sa_handler)(int);  /* Signal handler function */
    uint32_t sa_mask;         /* Signals to block during handler execution */
    int sa_flags;             /* Signal flags (future use) */
};

/**
 * struct sigcontext - Signal context saved on stack
 * Stores CPU state before signal handler is called
 */
struct sigcontext {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rax;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t orig_rax;    /* Original syscall number (if from syscall) */
    uint64_t rip;         /* Instruction pointer */
    uint64_t cs;          /* Code segment */
    uint64_t rflags;      /* CPU flags */
    uint64_t rsp;         /* Stack pointer */
    uint64_t ss;          /* Stack segment */
};

/**
 * struct sigframe - Signal frame on userspace stack
 * Complete context saved when signal handler is invoked
 */
struct sigframe {
    void (*handler)(int);     /* Signal handler function */
    int signum;               /* Signal number */
    struct sigcontext ctx;    /* Saved CPU context */
};

/**
 * send_signal - Send a signal to a process
 * @pid: Target process ID
 * @signal: Signal number to send
 *
 * Returns: 0 on success, -1 on error
 */
int send_signal(int pid, int signal);

/**
 * handle_signals - Check and handle pending signals for current process
 *
 * Called by scheduler before context switch
 */
void handle_signals(void);

/**
 * register_signal_handler - Register a signal handler for current process
 * @signal: Signal number
 * @handler: Handler function pointer (userspace address)
 *
 * Returns: 0 on success, -1 on error
 */
int register_signal_handler(int signal, void (*handler)(int));

/**
 * signal_ignore - Ignore a signal for current process
 * @signal: Signal number to ignore
 *
 * Returns: 0 on success, -1 on error
 */
int signal_ignore(int signal);

#endif /* _IR0_SIGNALS_H */
