// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: signals.c
 * Description: Basic signal implementation
 */

#include "signals.h"
#include <kernel/process.h>
#include <drivers/serial/serial.h>
#include <ir0/copy_user.h>
#include <mm/paging.h>
#include <ir0/kmem.h>
#include <config.h>
#include <string.h>

/**
 * send_signal - Send signal to process
 */
int send_signal(int pid, int signal)
{
    /* Validate signal number */
    if (signal < 0 || signal >= _NSIG)
    {
        return -1;
    }

    /* Find target process by PID */
    process_t *proc = process_find_by_pid(pid);
    if (!proc)
    {
        return -1; /* Process not found */
    }

    /* Set signal bit in pending mask */
    proc->signal_pending |= SIGNAL_MASK(signal);

#if DEBUG_PROCESS
    serial_print("[SIGNAL] Sent signal to process\n");
#endif

    return 0;
}

/**
 * handle_signals - Handle pending signals
 * Called by scheduler before switching to process
 * 
 * Signals are handled in priority order:
 * 1. Unstoppable signals (SIGKILL, SIGSTOP)
 * 2. Error signals (SIGSEGV, SIGFPE, SIGILL, SIGBUS) - terminate process
 * 3. Termination signals (SIGTERM, SIGINT, SIGQUIT, SIGABRT)
 * 4. Other signals
 */
void handle_signals(void)
{
    process_t *current = process_get_current();
    if (!current)
    {
        return;
    }

    /* Check for pending signals */
    if (current->signal_pending == 0)
    {
        return;
    }

    /* SIGKILL - immediate termination, cannot be caught or ignored */
    if (current->signal_pending & SIGNAL_MASK(SIGKILL))
    {
#if DEBUG_PROCESS
        serial_print("[SIGNAL] SIGKILL received, terminating process\n");
#endif
        current->signal_pending &= ~SIGNAL_MASK(SIGKILL);
        process_exit(-1);
        return; /* Never returns */
    }

    /* SIGSTOP - stop process (cannot be caught) */
    if (current->signal_pending & SIGNAL_MASK(SIGSTOP))
    {
#if DEBUG_PROCESS
        serial_print("[SIGNAL] SIGSTOP received, stopping process\n");
#endif
        current->signal_pending &= ~SIGNAL_MASK(SIGSTOP);
        current->state = PROCESS_BLOCKED;
        return;
    }

    /* Error signals - terminate process immediately (prevent crashes) */
    if (current->signal_pending & SIGNAL_MASK(SIGSEGV))
    {
#if DEBUG_PROCESS
        serial_print("[SIGNAL] SIGSEGV received (segmentation fault), terminating process\n");
#endif
        current->signal_pending &= ~SIGNAL_MASK(SIGSEGV);
        process_exit(139); /* Exit code 139 = 128 + 11 (SIGSEGV) */
        return;
    }

    if (current->signal_pending & SIGNAL_MASK(SIGFPE))
    {
#if DEBUG_PROCESS
        serial_print("[SIGNAL] SIGFPE received (arithmetic error), terminating process\n");
#endif
        current->signal_pending &= ~SIGNAL_MASK(SIGFPE);
        process_exit(136); /* Exit code 136 = 128 + 8 (SIGFPE) */
        return;
    }

    if (current->signal_pending & SIGNAL_MASK(SIGILL))
    {
#if DEBUG_PROCESS
        serial_print("[SIGNAL] SIGILL received (illegal instruction), terminating process\n");
#endif
        current->signal_pending &= ~SIGNAL_MASK(SIGILL);
        process_exit(132); /* Exit code 132 = 128 + 4 (SIGILL) */
        return;
    }

    if (current->signal_pending & SIGNAL_MASK(SIGBUS))
    {
#if DEBUG_PROCESS
        serial_print("[SIGNAL] SIGBUS received (bus error), terminating process\n");
#endif
        current->signal_pending &= ~SIGNAL_MASK(SIGBUS);
        process_exit(135); /* Exit code 135 = 128 + 7 (SIGBUS) */
        return;
    }

    /* Termination signals */
    if (current->signal_pending & SIGNAL_MASK(SIGTERM))
    {
#if DEBUG_PROCESS
        serial_print("[SIGNAL] SIGTERM received, terminating process\n");
#endif
        current->signal_pending &= ~SIGNAL_MASK(SIGTERM);
        process_exit(0);
        return;
    }

    if (current->signal_pending & SIGNAL_MASK(SIGINT))
    {
#if DEBUG_PROCESS
        serial_print("[SIGNAL] SIGINT received, terminating process\n");
#endif
        current->signal_pending &= ~SIGNAL_MASK(SIGINT);
        process_exit(130); /* Exit code 130 = 128 + 2 (SIGINT) */
        return;
    }

    if (current->signal_pending & SIGNAL_MASK(SIGQUIT))
    {
#if DEBUG_PROCESS
        serial_print("[SIGNAL] SIGQUIT received, terminating process\n");
#endif
        current->signal_pending &= ~SIGNAL_MASK(SIGQUIT);
        process_exit(131); /* Exit code 131 = 128 + 3 (SIGQUIT) */
        return;
    }

    if (current->signal_pending & SIGNAL_MASK(SIGABRT))
    {
#if DEBUG_PROCESS
        serial_print("[SIGNAL] SIGABRT received, terminating process\n");
#endif
        current->signal_pending &= ~SIGNAL_MASK(SIGABRT);
        process_exit(134); /* Exit code 134 = 128 + 6 (SIGABRT) */
        return;
    }

    /* SIGCONT - continue if stopped */
    if (current->signal_pending & SIGNAL_MASK(SIGCONT))
    {
#if DEBUG_PROCESS
        serial_print("[SIGNAL] SIGCONT received, resuming process\n");
#endif
        current->signal_pending &= ~SIGNAL_MASK(SIGCONT);
        if (current->state == PROCESS_BLOCKED)
        {
            current->state = PROCESS_READY;
        }
    }

    /* SIGCHLD - child process terminated (handled by parent, clear here) */
    if (current->signal_pending & SIGNAL_MASK(SIGCHLD))
    {
#if DEBUG_PROCESS
        serial_print("[SIGNAL] SIGCHLD received (child terminated)\n");
#endif
        /* SIGCHLD is informational, parent should handle via wait() */
        current->signal_pending &= ~SIGNAL_MASK(SIGCHLD);
    }

    /* Check for signals with userspace handlers */
    for (int sig = 1; sig < _NSIG; sig++)
    {
        if (current->signal_pending & SIGNAL_MASK(sig))
        {
            /* Check if signal is ignored */
            if (current->signal_ignored & SIGNAL_MASK(sig))
            {
                current->signal_pending &= ~SIGNAL_MASK(sig);
                continue;
            }
            
            /* Check if signal is blocked */
            if (current->signal_mask & SIGNAL_MASK(sig))
            {
                continue; /* Don't deliver blocked signals */
            }
            
            /* Check if there's a userspace handler */
            if (current->signal_handlers[sig] && 
                current->signal_handlers[sig] != SIG_DFL &&
                current->signal_handlers[sig] != SIG_IGN)
            {
                /* Call userspace handler */
                void (*handler)(int) = current->signal_handlers[sig];
                
                /* Validate handler is in userspace */
                if (is_user_address((void *)handler, sizeof(void *)))
                {
#if DEBUG_PROCESS
                    serial_print("[SIGNAL] Setting up signal frame for signal ");
                    serial_print_hex32((uint32_t)sig);
                    serial_print("\n");
#endif
                    /* Only setup signal frame for USER_MODE processes */
                    if (current->mode == USER_MODE)
                    {
                        /* Save current context */
                        struct sigcontext *ctx = kmalloc(sizeof(struct sigcontext));
                        if (!ctx)
                        {
                            /* Out of memory - fall back to default handler */
                            current->signal_handlers[sig] = SIG_DFL;
                            current->signal_pending &= ~SIGNAL_MASK(sig);
                            continue;
                        }
                        
                        /* Save CPU state from task structure */
                        ctx->r15 = current->task.r15;
                        ctx->r14 = current->task.r14;
                        ctx->r13 = current->task.r13;
                        ctx->r12 = current->task.r12;
                        ctx->rbp = current->task.rbp;
                        ctx->rbx = current->task.rbx;
                        ctx->r11 = current->task.r11;
                        ctx->r10 = current->task.r10;
                        ctx->r9 = current->task.r9;
                        ctx->r8 = current->task.r8;
                        ctx->rax = current->task.rax;
                        ctx->rcx = current->task.rcx;
                        ctx->rdx = current->task.rdx;
                        ctx->rsi = current->task.rsi;
                        ctx->rdi = current->task.rdi;
                        ctx->orig_rax = 0;  /* orig_rax not stored in task_t - set to 0 */
                        ctx->rip = current->task.rip;
                        ctx->cs = current->task.cs;
                        ctx->rflags = current->task.rflags;
                        ctx->rsp = current->task.rsp;
                        ctx->ss = current->task.ss;
                        
                        /* Store saved context in process */
                        current->saved_context = ctx;
                        
                        /* Allocate signal frame on userspace stack */
                        uint64_t frame_addr = current->task.rsp - sizeof(struct sigframe);
                        frame_addr &= ~0xF;  /* Align to 16 bytes (ABI requirement) */
                        
                        /* Ensure frame is in userspace */
                        if (frame_addr < 0x400000UL || frame_addr > 0x7FFFFFFFFFFFUL)
                        {
                            /* Invalid stack - fall back to default handler */
                            kfree(ctx);
                            current->saved_context = NULL;
                            current->signal_handlers[sig] = SIG_DFL;
                            current->signal_pending &= ~SIGNAL_MASK(sig);
                            continue;
                        }
                        
                        /* Setup signal frame */
                        struct sigframe frame;
                        frame.handler = handler;
                        frame.signum = sig;
                        frame.ctx = *ctx;
                        
                        /* Copy frame to userspace stack */
                        uint64_t old_cr3 = get_current_page_directory();
                        load_page_directory((uint64_t)current->page_directory);
                        memcpy((void *)frame_addr, &frame, sizeof(struct sigframe));
                        load_page_directory(old_cr3);
                        
                        /* Modify task state to invoke handler */
                        current->task.rsp = frame_addr;
                        current->task.rip = (uint64_t)handler;  /* Jump to handler */
                        current->task.rdi = sig;  /* First argument: signal number */
                        /* RSI/RDX/RCX/R8/R9 = 0 (other args, unused for signal handlers) */
                        
                        /* Clear signal before calling handler */
                        current->signal_pending &= ~SIGNAL_MASK(sig);
                        
#if DEBUG_PROCESS
                        serial_print("[SIGNAL] Signal frame set up, handler will be called\n");
#endif
                        /* Handler will be invoked on next context switch */
                        /* When handler returns, it will call sigreturn() syscall */
                    }
                    else
                    {
                        /* KERNEL_MODE: call directly (for dbgshell) */
                        current->signal_pending &= ~SIGNAL_MASK(sig);
                        handler(sig);
                    }
                }
                else
                {
                    /* Invalid handler address - use default */
                    current->signal_handlers[sig] = SIG_DFL;
                    current->signal_pending &= ~SIGNAL_MASK(sig);
                }
            }
        }
    }
    
    /* SIGALRM, SIGUSR1, SIGUSR2, SIGTRAP - handled above or default */
}

/**
 * register_signal_handler - Register a signal handler for current process
 */
int register_signal_handler(int signal, void (*handler)(int))
{
    if (signal < 1 || signal >= _NSIG)
        return -1;
    
    /* Signals that cannot be caught */
    if (signal == SIGKILL || signal == SIGSTOP)
        return -1;
    
    process_t *current = process_get_current();
    if (!current)
        return -1;
    
    /* Validate handler is in userspace (for USER_MODE processes) */
    if (current->mode == USER_MODE && handler != SIG_DFL && handler != SIG_IGN)
    {
        if (!is_user_address((void *)handler, sizeof(void *)))
        {
            return -1; /* Invalid handler address */
        }
    }
    
    current->signal_handlers[signal] = handler;
    
    /* If handler is SIG_IGN, add to ignored mask */
    if (handler == SIG_IGN)
    {
        current->signal_ignored |= SIGNAL_MASK(signal);
    }
    else
    {
        current->signal_ignored &= ~SIGNAL_MASK(signal);
    }
    
    return 0;
}

/**
 * signal_ignore - Ignore a signal for current process
 */
int signal_ignore(int signal)
{
    if (signal < 1 || signal >= _NSIG)
        return -1;
    
    /* Signals that cannot be ignored */
    if (signal == SIGKILL || signal == SIGSTOP)
        return -1;
    
    return register_signal_handler(signal, SIG_IGN);
}
