/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: signals.h
 * Description: IR0 kernel source/header file
 */

// SPDX-License-Identifier: GPL-3.0-only

#ifndef _IR0_SIGNALS_H
#define _IR0_SIGNALS_H

#include <stdint.h>
#include <stddef.h>

typedef struct process process_t;

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
#define SIGHUP    1   /* Hangup on controlling terminal */
#define SIGQUIT   3   /* Quit from keyboard (Ctrl+\\) */

/* Process control */
#define SIGCHLD  17   /* Child process terminated or stopped */
#define SIGSTOP  19   /* Stop process (cannot be caught) */
#define SIGCONT  18   /* Continue if stopped */
#define SIGTSTP  20   /* Stop typed at terminal */
#define SIGTTIN  21   /* Terminal input for background process */
#define SIGTTOU  22   /* Terminal output for background process */

/* Other */
#define SIGABRT   6   /* Abort signal (from abort()) */
#define SIGALRM  14   /* Timer signal (from alarm()) */
#define SIGUSR1  10   /* User-defined signal 1 */
#define SIGUSR2  12   /* User-defined signal 2 */
#define SIGPIPE  13   /* Write on a pipe with no readers (Linux signal(7)) */
#define SIGURG   23   /* Urgent data on a socket (default: ignore) */
#define SIGWINCH 28   /* Window size change (TTY) */
#define SIGIO    29   /* I/O readiness notification (F_SETOWN/O_ASYNC) */

/*
 * Linux sigset layout: signal N is bit N-1, so signals 1..64 fill one
 * uint64_t. The pending, blocked and ignored sets use it unchanged at the
 * ABI boundary.
 */
#define SIGNAL_MASK(sig) (1ULL << ((sig) - 1))

/* Never blocked, caught or ignored (Linux sigdelsetmask(SIGKILL|SIGSTOP)). */
#define SIGNAL_UNBLOCKABLE (SIGNAL_MASK(SIGKILL) | SIGNAL_MASK(SIGSTOP))

/* Linux _NSIG: valid signals are 1.._NSIG inclusive (valid_signal()). */
#define _NSIG 64

/* Real-time signals. Standard signals stay 1..31. */
#define SIGRTMIN 32
#define SIGRTMAX _NSIG

_Static_assert(SIGRTMIN == 32, "SIGRTMIN");

/* sigset_t — Linux uapi layout (128 bytes on x86-64). */
#define _IR0_SIGSET_WORDS 16

typedef struct
{
	unsigned long __val[_IR0_SIGSET_WORDS];
} sigset_t;

/* rt_sigprocmask how argument */
#define SIG_BLOCK     0  /* Add signals to mask */
#define SIG_UNBLOCK   1  /* Remove signals from mask */
#define SIG_SETMASK   2  /* Replace mask */

/* Special signal handler values */
#define SIG_DFL ((void (*)(int))0)  /* Default handler */
#define SIG_IGN ((void (*)(int))1)  /* Ignore signal */
#define SIG_ERR ((void (*)(int))-1) /* Error return */

#define SA_SIGINFO    4
#define SA_ONSTACK    0x08000000U
#define SA_NODEFER    0x40000000U
#define SA_RESTORER   0x04000000U
#define SA_RESTART    0x10000000U
#define SA_RESETHAND  0x80000000U

/* sigaltstack(2) ss_flags. SS_ONSTACK is only reported, never installed. */
#define SS_ONSTACK    1
#define SS_DISABLE    2

/*
 * Linux stack_t (LP64): ss_sp, ss_flags, then ss_size at offset 16.
 * Either pointer argument of sigaltstack(2) may be NULL.
 */
struct sigaltstack {
	void *ss_sp;
	int ss_flags;
	size_t ss_size;
};

_Static_assert(sizeof(struct sigaltstack) == 24, "stack_t size");
_Static_assert(offsetof(struct sigaltstack, ss_flags) == 8, "ss_flags");
_Static_assert(offsetof(struct sigaltstack, ss_size) == 16, "ss_size");

/* Linux uapi si_code for SIGSEGV (subset). */
#define SEGV_MAPERR   1
#define SEGV_ACCERR   2

/* Linux uapi si_code for the other synchronous faults (subset). */
#define ILL_ILLOPC    1
#define ILL_ILLOPN    2
#define FPE_INTDIV    1
#define BUS_ADRALN    1
#define BUS_ADRERR    2
#define TRAP_BRKPT    1
#define TRAP_TRACE    2

/* Linux uapi si_code of the senders (<= 0 come from userspace). */
#define SI_USER       0
#define SI_KERNEL     0x80
#define SI_QUEUE      (-1)
#define SI_TKILL      (-6)

/*
 * siginfo_t — Linux layout: 128 bytes, the union starts at offset 16
 * (__ARCH_SI_PREAMBLE_SIZE on 64-bit): _kill for kill/tkill, _rt for
 * sigqueue, _sigfault for the synchronous faults.
 */
typedef struct
{
	int si_signo;
	int si_errno;
	int si_code;
	union
	{
		char _pad[128 - 4 * (int)sizeof(int)];
		struct
		{
			int32_t si_pid;
			uint32_t si_uid;
		} _kill;
		struct
		{
			int32_t si_pid;
			uint32_t si_uid;
			uint64_t si_value;
		} _rt;
		struct
		{
			void *si_addr;
		} _sigfault;
	} _sifields;
} siginfo_t;

_Static_assert(sizeof(siginfo_t) == 128, "siginfo_t must match Linux");
_Static_assert(offsetof(siginfo_t, _sifields) == 16, "siginfo_t preamble");

/* One queued signal instance (Linux struct sigqueue). */
struct sigqueue
{
	struct sigqueue *next;
	siginfo_t info;
};

/**
 * struct sigaction — Linux rt_sigaction layout (kernel struct sigaction size).
 */
struct sigaction {
    void (*sa_handler)(int);
    unsigned long sa_flags;
    void (*sa_restorer)(void);
    sigset_t sa_mask;
};

/*
 * ISA-specific sigcontext (Linux uapi: x86-64 and aarch64).
 * Portable code: arch_task_load/store_sigcontext / arch_signal_* only.
 */
#include <asm/sigcontext.h>

/**
 * struct sigframe - Signal frame on userspace stack
 * Complete context saved when signal handler is invoked; rt_sigreturn reads
 * it back from the handler's SP.
 *
 * Layout on stack (low→high): [return slot][sigframe…]. The return slot is
 * SIGFRAME_RETURN_SLOT bytes: the 8-byte restorer on x86-64, empty on arm64
 * (x30 holds it). sizeof must be 0 mod 16 so that after aligning SP the
 * handler entry SP is 8 mod 16 on x86-64 (SysV psABI: as after a call, so
 * aligned SSE stores in the handler do not #GP) and 16-byte aligned on
 * arm64 (AAPCS64).
 */
struct sigframe {
    void (*handler)(int);     /* Signal handler function */
    int signum;               /* Signal number */
    int __pad0;
#ifdef IR0_SIGFRAME_CTX_IS_MCONTEXT
    /*
     * Pre-delivery signal_mask (Linux ucontext uc_sigmask role, compact).
     * rt_sigreturn restores this; also used if userspace abandons the frame.
     */
    uint64_t oldmask;
    siginfo_t info;           /* SA_SIGINFO second argument */
    /*
     * Linux ucontext_t header. ctx is the mcontext and follows this array
     * with no gap, so the handler's ucontext and rt_sigreturn share one
     * copy. A second sigcontext does not fit a SIGSTKSZ stack once a
     * handler nests another signal.
     */
    unsigned char ucontext[IR0_UCONTEXT_MCONTEXT_OFF]
	    __attribute__((aligned(16)));
    struct sigcontext ctx;
#else
    struct sigcontext ctx;    /* Saved CPU context */
    /*
     * Pre-delivery signal_mask (Linux ucontext uc_sigmask role, compact).
     * rt_sigreturn restores this; also used if userspace abandons the frame.
     */
    uint64_t oldmask;
    siginfo_t info;           /* SA_SIGINFO second argument */
    /*
     * SA_SIGINFO third argument: Linux ucontext_t. rt_sigreturn resumes
     * from its mcontext, so the handler can edit the restored registers.
     * The stored size is rounded up so the whole frame stays 16-aligned.
     * The ISA sigcontext above stays too: its layout is not the mcontext.
     */
    unsigned char ucontext[(IR0_UCONTEXT_BYTES + 15u) & ~15u];
#endif
};

_Static_assert((sizeof(struct sigframe) % 16) == 0,
	       "sigframe size must be 16-byte multiple for handler RSP ABI");
#ifdef IR0_SIGFRAME_CTX_IS_MCONTEXT
_Static_assert(offsetof(struct sigframe, ctx) ==
		       offsetof(struct sigframe, ucontext) +
			       IR0_UCONTEXT_MCONTEXT_OFF,
	       "sigframe ctx must be the ucontext mcontext");
_Static_assert(IR0_UCONTEXT_MCONTEXT_OFF + sizeof(struct sigcontext) ==
		       IR0_UCONTEXT_BYTES,
	       "ucontext must be the header plus one mcontext");
#endif

/**
 * send_signal - Send a kernel-raised signal (SI_KERNEL) to a process
 * @pid: Target process ID
 * @signal: Signal number to send
 *
 * Returns: 0 on success, -1 on error
 */
int send_signal(int pid, int signal);

/**
 * send_sig_info - Queue @sig with @info for @t (Linux send_sig_info()).
 *
 * Ignored signals are dropped, a standard signal already pending is not
 * queued again, real-time signals queue one entry per send. Wakes @t
 * when it does not block @sig. Returns 0, or -EAGAIN when a real-time
 * signal from sigqueue cannot be queued.
 */
int send_sig_info(int sig, const siginfo_t *info, process_t *t);

/* Every signal pending for @p, blocked or not (Linux sigpending set). */
uint64_t signal_pending_set(const process_t *p);

/* Drop the signals in @mask from @p's pending set with their siginfo. */
void signal_flush(process_t *p, uint64_t mask);

/**
 * send_signal_pgrp - Deliver @signal to every live process with pgid == @pgid.
 * Returns count of processes signaled (≥0).
 */
int send_signal_pgrp(int32_t pgid, int signal);

/**
 * signal_exit_current - Terminate the current task by @sig (default action).
 *
 * The single exit path for a fatal signal: wait(2) reports
 * WIFSIGNALED(@sig). Callers decide that no handler applies; this never
 * returns.
 */
void signal_exit_current(int sig) __attribute__((noreturn));

struct ir0_fault;

/**
 * signal_deliver_fault - Report a synchronous fault of the current task and
 * hand @sig to its user handler.
 * @irq_frame: ISA exception frame to redirect into the handler, or NULL to
 *             queue @sig for delivery on the next return to userspace.
 *
 * Logs USER_FAULT_FRAME first. Returns 1 when a handler will run, 0 when
 * the default action applies (the caller then calls signal_exit_current()).
 */
int signal_deliver_fault(int sig, const struct ir0_fault *fault,
			 void *irq_frame);

/**
 * force_sig_fault - Synchronous fault: run the handler or apply the default
 * action. Returns only when a handler will run (Linux force_sig_fault()).
 */
void force_sig_fault(int sig, const struct ir0_fault *fault, void *irq_frame);

/**
 * do_signal - Signal work on the way out of a syscall (Linux
 * arch_do_signal_or_restart()).
 * @p: the current task; its syscall frame holds the syscall's result
 * @interrupted: NULL at a syscall exit. rt_sigreturn passes the context it
 *               restored: that is the state a handler frame saves, and no
 *               syscall result is settled or restarted.
 *
 * Applies default actions, then either sets the frame up to enter the next
 * caught signal's handler, or restarts an interrupted syscall. A result of
 * -ERESTARTSYS / -ERESTARTNOHAND / -ERESTARTNOINTR is settled here: the
 * call restarts or fails with EINTR, never reaching userspace as such.
 */
void do_signal(process_t *p, const struct sigcontext *interrupted);

/*
 * signal_pending - @p has a signal that must end an interruptible sleep
 * (Linux signal_pending()). Blocking syscalls test it after setting
 * PROCESS_BLOCKED and return -ERESTARTSYS (or -EINTR) when it is set.
 */
int signal_pending(process_t *p);

/**
 * set_current_blocked - Install @mask as the current task's blocked set
 * (Linux set_current_blocked()). SIGKILL and SIGSTOP are dropped from it:
 * every path that takes a mask from userspace goes through here.
 */
void set_current_blocked(uint64_t mask);

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

/*
 * Returns 1 if @sig has a deliverable userspace handler (not DFL/IGN/blocked).
 */
int signals_has_user_handler(process_t *p, int sig);

/** execve: drop pending signals and handlers; keep signal_mask. */
void signals_reset_on_exec(process_t *p);

/** Restore process signal_mask after rt_sigreturn (paired with delivery). */

/**
 * Linux-like: if userspace left the handler stack without rt_sigreturn
 * (e.g. longjmp), drop kernel sigframe bookkeeping. Call on syscall entry
 * except rt_sigreturn itself.
 */
void signals_try_abandon_sigframe(process_t *p);

/*
 * Deliver @sig synchronously from a user #PF IRQ @frame (isr_common_stub layout).
 * @fault_addr is the ISA-decoded faulting virtual address (stored in siginfo
 * for SA_SIGINFO). @si_code is the Linux si_code (SEGV_MAPERR / SEGV_ACCERR)
 * or 0 when the signal is not a page fault.
 * Returns 1 if redirected to handler; 0 if caller should terminate/default.
 */
int signals_deliver_from_irq_frame(process_t *p, int sig, uint64_t *frame,
				   uint64_t fault_addr, int si_code);

/* Linux valid_signal() plus the "no signal 0" rule of every caller. */
static inline int valid_signal(int sig)
{
	return sig >= 1 && sig <= _NSIG;
}

/*
 * The kernel set is the first word of a Linux sigset_t, in the same
 * layout (signal N in bit N-1); the other words are always zero.
 */
static inline uint64_t ir0_sigset_word(const sigset_t *set)
{
	if (!set)
		return 0;
	return (uint64_t)set->__val[0];
}

static inline void ir0_sigset_set_word(sigset_t *set, uint64_t mask)
{
	size_t i;

	if (!set)
		return;

	for (i = 0; i < _IR0_SIGSET_WORDS; i++)
		set->__val[i] = 0;
	set->__val[0] = (unsigned long)mask;
}

#endif /* _IR0_SIGNALS_H */
