/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: signals.c
 * Description: Basic signal implementation
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/signals.h>
#include <ir0/mm_range.h>
#include <ir0/sched.h>
#include <ir0/clock.h>
#include <ir0/paging.h>
#include <ir0/signal_irq.h>
#include <ir0/sigtramp.h>
#include <ir0/fault.h>
#include <ir0/oops.h>
#include <ir0/process.h>
#include <ir0/process_domains.h>
#include <stddef.h>
#include <string.h>
#include "signal_diag.h"

/* Where signal_setup_frame() left the handler frame. */
struct signal_frame_setup
{
	uint64_t sp;        /* handler entry SP (bottom of the return slot) */
	uint64_t restorer;  /* handler return address */
	uint64_t info_addr; /* &frame->info */
	uint64_t uctx_addr; /* &frame->ucontext */
};

/* Why signal_setup_frame() could not build a frame (log tags). */
enum signal_frame_error
{
	SIGFRAME_OK = 0,
	SIGFRAME_BAD_SP,
	SIGFRAME_NO_RESTORER,
	SIGFRAME_COPY,
};

static int signal_setup_frame(process_t *p, int sig,
			      const struct sigcontext *ctx,
			      uint64_t fault_addr, int si_code,
			      struct signal_frame_setup *out);

/*
 * A handler or restorer is a code address in the task's user window. It is
 * never copied through, and kernel-mode callers (ktest) must not widen it,
 * so this is the window arithmetic, not access_ok().
 */
static int signal_code_addr_ok(const void *addr)
{
	return mm_user_range_ok((uintptr_t)addr, sizeof(void *),
				mm_user_va_start(), mm_user_va_end());
}

static int signal_ip_is_registered_handler(process_t *p, uint64_t ip)
{
	int i;

	if (!p || ip < 0x1000ul)
		return 0;
	for (i = 1; i < _NSIG; i++)
	{
		void (*h)(int) = p->sighand->handlers[i];

		if (!h || h == SIG_DFL || h == SIG_IGN)
			continue;
		if ((uint64_t)(uintptr_t)h == ip)
			return 1;
	}
	return 0;
}

/*
 * The mask a handler frame saves (Linux sigmask_to_save()): the caller's own
 * mask while rt_sigsuspend runs with a temporary one.
 */
static uint64_t sigmask_to_save(const process_t *p)
{
	return p->restore_sigmask ? p->saved_sigmask : p->signal_mask;
}

static void signals_apply_handler_mask(process_t *p, int sig, uint32_t sa_flags)
{
	if (!p || sig < 1 || sig >= _NSIG)
		return;
	if (!p->signal_mask_saved_valid)
	{
		p->signal_mask_saved = sigmask_to_save(p);
		p->signal_mask_saved_valid = 1;
	}
	p->signal_mask |= p->sighand->sa_mask[sig];
	if (!(sa_flags & SA_NODEFER))
		p->signal_mask |= SIGNAL_MASK(sig);
}

void signals_try_abandon_sigframe(process_t *p)
{
	uint64_t usp;
	uint64_t frame_sp;

	if (!p)
		return;
	frame_sp = p->signal_frame_sp;
	if (frame_sp == 0)
		return;

	/*
	 * Still on the handler stack if SP is at or below the top of the
	 * sigframe (stack grows down). longjmp back to the interrupted site
	 * restores a higher SP — that is the Linux "abandoned trampoline"
	 * case where rt_sigreturn never runs.
	 */
	usp = process_syscall_sp(p);
	if (usp != 0 && usp <= frame_sp + sizeof(struct sigframe) + 128UL)
		return;

	signal_trace(SIGNAL_TRACE_ABANDON, p, 0, usp, frame_sp, 0);

	/*
	 * Linux keeps no per-frame state, so leaving a handler this way
	 * changes nothing for the task: the handler mask stays in force
	 * (signal(7); siglongjmp(3) restores a saved mask from userspace) and
	 * no other subsystem hears about it. Only IR0's own frame
	 * bookkeeping goes, so the next frame saves the mask in force then.
	 */
	p->signal_mask_saved_valid = 0;
	p->signal_frame_sp = 0;
}

int signals_has_user_handler(process_t *p, int sig)
{
	void (*handler)(int);

	if (!p || sig < 1 || sig >= _NSIG)
		return 0;
	if (p->signal_ignored & SIGNAL_MASK(sig))
		return 0;
	if (p->signal_mask & SIGNAL_MASK(sig))
		return 0;
	handler = p->sighand->handlers[sig];
	if (!handler || handler == SIG_DFL || handler == SIG_IGN)
		return 0;
	if (p->mode == USER_MODE &&
	    !signal_code_addr_ok((const void *)handler))
		return 0;
	return 1;
}

int signals_deliver_from_irq_frame(process_t *p, int sig, uint64_t *frame,
				   uint64_t fault_addr, int si_code)
{
	void (*handler)(int);
	struct sigcontext ctx;
	struct signal_frame_setup setup;
	uint32_t sa_flags;

	if (!signals_has_user_handler(p, sig))
		return 0;
	if (!frame || p->mode != USER_MODE)
		return 0;

	handler = p->sighand->handlers[sig];
	sa_flags = p->sighand->sa_flags[sig];

	memset(&ctx, 0, sizeof(ctx));
	signal_fill_sigcontext_from_irq_frame(&ctx, frame);
	if (signal_ip_is_registered_handler(p, sigcontext_ip(&ctx)))
	{
		if (sig == SIGSEGV || sig == SIGBUS || sig == SIGILL ||
		    sig == SIGFPE)
		{
			signal_exit_current(sig);
		}
		return 0;
	}

	/*
	 * Linux setup_rt_frame() then force_sigsegv() on failure: the frame
	 * is what lets the handler return (rt_sigreturn reads it back), so a
	 * fault whose frame cannot be built ends the task by SIGSEGV.
	 */
	if (signal_setup_frame(p, sig, &ctx, fault_addr, si_code, &setup) !=
	    SIGFRAME_OK)
		signal_exit_current(SIGSEGV);

	signal_redirect_irq_frame(frame, (void *)handler, sig, setup.sp,
				  setup.info_addr, setup.uctx_addr,
				  (sa_flags & SA_SIGINFO) ? 1 : 0,
				  setup.restorer);
	process_save_user_exception_frame(frame);

	if (sa_flags & SA_RESETHAND)
		p->sighand->handlers[sig] = SIG_DFL;

	p->signal_frame_sp = setup.sp;
	signals_apply_handler_mask(p, sig, sa_flags);
	p->signal_pending &= ~SIGNAL_MASK(sig);

	signal_trace(SIGNAL_TRACE_DELIVER_FAULT, p, sig, fault_addr,
		     sigcontext_ip(&ctx), (uint64_t)(uintptr_t)handler);

	return 1;
}

int signal_deliver_fault(int sig, const struct ir0_fault *fault,
			 void *irq_frame)
{
	process_t *p = current_process;

	if (!p || p->mode != USER_MODE)
		panic("fault signal without a user task");
	ir0_log_user_fault(fault, (unsigned int)p->task.pid, p->comm);

	/*
	 * With a frame the faulting context is redirected into the handler
	 * now; replaying the instruction first would fault again.
	 */
	if (irq_frame)
	{
		int si_code = 0;

		if (fault && fault->kind == IR0_FAULT_PAGE)
			si_code = fault->present ? SEGV_ACCERR : SEGV_MAPERR;
		else if (fault && fault->si_code != 0)
			si_code = fault->si_code;
		else if (fault && fault->addr != 0)
			si_code = SEGV_MAPERR;
		return signals_deliver_from_irq_frame(p, sig, irq_frame,
						      fault ? fault->addr : 0,
						      si_code);
	}
	if (signals_has_user_handler(p, sig))
	{
		p->signal_pending |= SIGNAL_MASK(sig);
		return 1;
	}
	return 0;
}

void force_sig_fault(int sig, const struct ir0_fault *fault, void *irq_frame)
{
	if (signal_deliver_fault(sig, fault, irq_frame))
		return;
	signal_exit_current(sig);
}

void signals_reset_on_exec(process_t *p)
{
	int i;

	if (!p)
		return;
	p->signal_pending = 0;
	p->signal_mask = 0;
	p->signal_ignored = 0;
	p->signal_mask_saved = 0;
	p->signal_mask_saved_valid = 0;
	p->signal_frame_sp = 0;
	p->sas_ss_sp = 0;
	p->sas_ss_size = 0;
	p->sas_ss_flags = SS_DISABLE;
	p->it_real_expire_ms = 0;
	p->it_real_interval_ms = 0;
	/*
	 * Linux execve does not keep a shared handler table. Reset only the
	 * private copy so another task keeps the handlers it installed.
	 */
	if (process_signals_unshare(p) < 0 || !p->sighand)
		return;
	for (i = 0; i < _NSIG; i++)
	{
		p->sighand->handlers[i] = SIG_DFL;
		p->sighand->sa_flags[i] = 0;
		p->sighand->sa_mask[i] = 0;
		p->sighand->restorer[i] = NULL;
	}
}

/* Default action Ign (Linux sig_kernel_ignore()). */
static int signal_default_ignored(int sig)
{
	return sig == SIGCHLD || sig == SIGWINCH || sig == SIGCONT ||
	       sig == SIGURG;
}

/* Default action Stop (Linux sig_kernel_stop()). */
static int signal_default_stops(int sig)
{
	return sig == SIGSTOP || sig == SIGTSTP || sig == SIGTTIN ||
	       sig == SIGTTOU;
}

/*
 * signal_pending - @p has a signal that must end an interruptible sleep
 * (Linux signal_pending()): pending and not blocked, or SIGKILL. Ignored
 * signals do not count, nor do those whose default action is to ignore
 * them (SIGCHLD, SIGWINCH, SIGCONT, SIGURG) while they have no handler.
 */
int signal_pending(process_t *p)
{
	uint64_t deliverable;
	int sig;

	if (!p || p->signal_pending == 0)
		return 0;
	if (p->signal_pending & SIGNAL_MASK(SIGKILL))
		return 1;

	deliverable = p->signal_pending & ~p->signal_mask & ~p->signal_ignored;
	for (sig = 1; sig < _NSIG && deliverable; sig++)
	{
		void (*handler)(int) = p->sighand->handlers[sig];
		int caught = handler && handler != SIG_DFL && handler != SIG_IGN;

		if (!(deliverable & SIGNAL_MASK(sig)))
			continue;
		if (!caught && signal_default_ignored(sig))
			deliverable &= ~SIGNAL_MASK(sig);
	}
	return deliverable != 0;
}

/**
 * send_signal - Send signal to process
 */
int send_signal(int pid, int signal)
{
    process_t *proc;

    /* Validate signal number */
    if (signal < 0 || signal >= _NSIG)
    {
        return -1;
    }

    /* Find target process by PID */
    proc = process_find_by_pid(pid);
    if (!proc)
    {
        return -1; /* Process not found */
    }

    /*
     * kill(pid, 0): existence probe only (Linux). Must not set pending,
     * wake blocked tasks, or run default-fatal teardown.
     */
    if (signal == 0)
	return 0;

    signal_trace(SIGNAL_TRACE_SEND, proc, signal, 0, 0, 0);

    /*
     * kill(2): "The only signals that can be sent to process ID 1, the init
     * process, are those for which init has explicitly installed signal
     * handlers." Linux sig_task_ignored() drops the rest at send time
     * instead of leaving them pending.
     */
    if (proc->task.pid == 1)
    {
	void (*handler)(int) = proc->sighand->handlers[signal];

	if (!handler || handler == SIG_DFL || handler == SIG_IGN)
		return 0;
    }

    /*
     * Default-fatal: zombieize immediately. Do not promote BLOCKED→READY
     * first (that raced schedule into a half-dead task → #UD on iret).
     */
    if (process_signal_is_default_fatal(proc, signal))
    {
	if (process_signal_default_kill(proc, signal))
	{
	    clock_request_sched_resched();
	    return 0;
	}
	/* Ignored / caught — fall through to pending delivery. */
    }

    proc->signal_pending |= SIGNAL_MASK(signal);

    /*
     * Wake blocked tasks so caught signals / pause(2) can run handlers.
     */
    if (proc->state == PROCESS_BLOCKED)
    {
	process_set_sched_state(proc, PROCESS_READY);
	sched_promote(proc);
	clock_request_sched_resched();
	signal_trace(SIGNAL_TRACE_WAKE, proc, signal, 0, 0, 0);
    }

    return 0;
}

int send_signal_pgrp(int32_t pgid, int signal)
{
	process_t *p;
	int n = 0;

	if (pgid <= 0 || signal < 0 || signal >= _NSIG)
		return 0;

	for (p = process_list; p; p = p->next)
	{
		if ((int32_t)p->pgid != pgid)
			continue;
		if (send_signal((int)p->task.pid, signal) == 0)
			n++;
	}
	return n;
}

/*
 * Linux get_sigframe() (arch/x86/kernel/signal.c, arch/arm64/kernel/
 * signal.c). SA_ONSTACK with an enabled alternate stack, and the
 * interrupted SP not already inside it, places the frame at the top of
 * that stack (no red zone). Otherwise the frame goes just below the
 * interrupted stack pointer, past the ABI red zone. Below the frame sits
 * the return slot on ISAs whose handlers return with a stack pop; the
 * handler starts with SP there. A frame that cannot be written makes the
 * copy fail and the task gets SIGSEGV, as with force_sigsegv().
 *
 * Returns the handler's SP and the frame address, or 0 when the chosen
 * stack cannot hold a frame.
 */
static uint64_t signal_pick_handler_sp(process_t *p, int sig,
				       uint64_t saved_sp,
				       uint64_t *frame_addr_out)
{
	uint64_t frame_addr;
	uint64_t sp;
	uint64_t user_sp;
	int alt;

	alt = 0;
	sp = saved_sp;
	if (p && p->sighand && sig >= 1 && sig < _NSIG &&
	    (p->sighand->sa_flags[sig] & SA_ONSTACK) &&
	    p->sas_ss_size != 0 &&
	    !process_on_altstack(p, saved_sp))
	{
		sp = p->sas_ss_sp + p->sas_ss_size;
		alt = 1;
	}

	if (!alt)
	{
		if (sp < SIGFRAME_RED_ZONE + sizeof(struct sigframe) +
				SIGFRAME_RETURN_SLOT + 16UL)
			return 0;
		frame_addr = (sp - SIGFRAME_RED_ZONE -
			      sizeof(struct sigframe)) & ~0xFUL;
		user_sp = frame_addr - SIGFRAME_RETURN_SLOT;
	}
	else
	{
		if (sp < sizeof(struct sigframe) + SIGFRAME_RETURN_SLOT)
			return 0;
		frame_addr = (sp - sizeof(struct sigframe)) & ~0xFUL;
		user_sp = frame_addr - SIGFRAME_RETURN_SLOT;
		if (user_sp < p->sas_ss_sp || frame_addr < p->sas_ss_sp)
			return 0;
	}

	*frame_addr_out = frame_addr;
	return user_sp;
}

/*
 * Return address for @sig's handler: SA_RESTORER when the caller supplied
 * one, otherwise the kernel trampoline (Linux arm64 vDSO sigtramp). 0 when
 * the ISA requires SA_RESTORER and none was given (Linux x86-64).
 */
static uint64_t signal_handler_restorer(process_t *p, int sig)
{
	void (*fn)(void) = p->sighand->restorer[sig];

	if (!fn)
		return sigtramp_addr();
	if (!signal_code_addr_ok((const void *)fn))
		return 0;
	return (uint64_t)(uintptr_t)fn;
}

/*
 * signal_setup_frame - Write @sig's handler frame for the interrupted
 * context @ctx to the user stack (Linux setup_rt_frame()).
 *
 * The one frame writer for every delivery: asynchronous signals and
 * synchronous CPU faults. Below the frame sits the return slot holding
 * the restorer on ISAs that return through the stack; rt_sigreturn finds
 * the frame at the handler's SP after the handler returns. @fault_addr is
 * the si_addr reported to an SA_SIGINFO handler.
 *
 * Returns SIGFRAME_OK and fills @out, or why no frame was written.
 */
static int signal_setup_frame(process_t *p, int sig,
			      const struct sigcontext *ctx,
			      uint64_t fault_addr, int si_code,
			      struct signal_frame_setup *out)
{
	struct sigframe frame;
	uint64_t frame_addr;
	uint64_t user_sp;
	uint64_t restorer;

	user_sp = signal_pick_handler_sp(p, sig, sigcontext_sp(ctx), &frame_addr);
	if (user_sp == 0)
		return SIGFRAME_BAD_SP;
	restorer = signal_handler_restorer(p, sig);
	if (restorer == 0)
		return SIGFRAME_NO_RESTORER;

	memset(&frame, 0, sizeof(frame));
	frame.handler = p->sighand->handlers[sig];
	frame.signum = sig;
	frame.ctx = *ctx;
	/*
	 * rt_sigsuspend publishes the pre-suspend mask in signal_mask_saved
	 * before delivery. The handler must restore that mask, not the
	 * temporary one.
	 */
	frame.oldmask = p->signal_mask_saved_valid
		? (uint64_t)p->signal_mask_saved
		: (uint64_t)sigmask_to_save(p);
	frame.info.si_signo = sig;
	if (si_code != 0)
	{
		frame.info.si_code = si_code;
		frame.info._sifields._sigfault.si_addr =
			(void *)(uintptr_t)fault_addr;
	}
	ucontext_export(ctx,
			     frame_addr + offsetof(struct sigframe, ucontext),
			     frame.oldmask, frame.ucontext);

	if (copy_to_user_mm(process_pgd(p), frame_addr, &frame,
			    sizeof(frame)) != 0 ||
	    (SIGFRAME_RETURN_SLOT != 0 &&
	     copy_to_user_mm(process_pgd(p), user_sp, &restorer,
			     SIGFRAME_RETURN_SLOT) != 0))
		return SIGFRAME_COPY;

	out->sp = user_sp;
	out->restorer = restorer;
	out->info_addr = frame_addr + offsetof(struct sigframe, info);
	out->uctx_addr = frame_addr + offsetof(struct sigframe, ucontext);
	return SIGFRAME_OK;
}

/*
 * Linux force_sigsegv(): a handler frame that cannot be set up becomes
 * SIGSEGV. Failing to set up SIGSEGV itself, or having no SIGSEGV handler,
 * terminates the task.
 */
static void signal_force_segv(process_t *p, int sig)
{
	if (sig == SIGSEGV || !signals_has_user_handler(p, SIGSEGV))
		signal_exit_current(SIGSEGV);
	p->signal_pending |= SIGNAL_MASK(SIGSEGV);
}

/* Pending signals whose default action is Terminate, in handling order. */
static const int signal_default_terminate[] = {
	SIGSEGV, SIGFPE, SIGILL, SIGBUS, SIGTERM, SIGINT, SIGHUP, SIGQUIT,
	SIGABRT, SIGPIPE,
};

static int signal_is_fault(int sig)
{
	return sig == SIGSEGV || sig == SIGBUS || sig == SIGILL || sig == SIGFPE;
}

/*
 * get_signal - Apply the default actions of @p's pending signals (Linux
 * get_signal()) and return the next one whose user handler must run, or 0.
 *
 * SIGKILL and every uncaught signal whose default action is Term or Core
 * end the task here (the fault signals first); SIGSTOP and an uncaught
 * SIGTSTP/SIGTTIN/SIGTTOU sleep until SIGCONT. Ignored signals, and
 * uncaught ones whose default is Ign, are dropped; blocked ones stay
 * pending until unblocked, so a blocked SIGTERM never kills the task.
 */
static int get_signal(process_t *p)
{
	size_t i;
	int sig;

	if (p->signal_pending & SIGNAL_MASK(SIGKILL))
		signal_exit_current(SIGKILL);

	if (p->signal_pending & SIGNAL_MASK(SIGSTOP))
	{
		p->signal_pending &= ~SIGNAL_MASK(SIGSTOP);
		process_set_sched_state(p, PROCESS_BLOCKED);
		schedule();
	}

	for (i = 0; i < sizeof(signal_default_terminate) /
			    sizeof(signal_default_terminate[0]); i++)
	{
		sig = signal_default_terminate[i];
		if (!(p->signal_pending & SIGNAL_MASK(sig)))
			continue;
		if (p->signal_ignored & SIGNAL_MASK(sig))
		{
			p->signal_pending &= ~SIGNAL_MASK(sig);
			continue;
		}
		if ((p->signal_mask & SIGNAL_MASK(sig)) ||
		    signals_has_user_handler(p, sig))
			continue;
		if (sig == SIGSEGV)
			signal_trace(SIGNAL_TRACE_KILL_SEGV, p, sig, 0, 0, 0);
		signal_exit_current(sig);
	}

	/* SIGCONT already woke a stopped task when it was sent. */
	p->signal_pending &= ~SIGNAL_MASK(SIGCONT);

	for (sig = 1; sig < _NSIG; sig++)
	{
		uint64_t bit = SIGNAL_MASK(sig);
		void (*handler)(int) = p->sighand->handlers[sig];

		if (!(p->signal_pending & bit))
			continue;
		if (p->signal_ignored & bit)
		{
			p->signal_pending &= ~bit;
			continue;
		}
		if (p->signal_mask & bit)
			continue;
		if (handler == SIG_IGN || (handler == SIG_DFL &&
					   signal_default_ignored(sig)))
		{
			p->signal_pending &= ~bit;
			continue;
		}
		if (handler == SIG_DFL && signal_default_stops(sig))
		{
			p->signal_pending &= ~bit;
			process_set_sched_state(p, PROCESS_BLOCKED);
			schedule();
			continue;
		}
		/* Every other default action terminates (signal(7) Term/Core). */
		if (handler == SIG_DFL)
			signal_exit_current(sig);
		if (!signal_code_addr_ok((const void *)handler))
		{
			/* A handler outside userspace falls back to the default. */
			p->sighand->handlers[sig] = SIG_DFL;
			p->signal_pending &= ~bit;
			continue;
		}
		return sig;
	}
	return 0;
}

/*
 * syscall_restart_or_eintr - Settle an interrupted syscall's result in @regs
 * (Linux handle_signal() / arch_do_signal_or_restart()). @handler says a
 * handler runs next; @sa_restart is its SA_RESTART. -ERESTARTSYS restarts
 * unless a handler without SA_RESTART runs, -ERESTARTNOHAND only when no
 * handler runs, -ERESTARTNOINTR always; otherwise the call fails with EINTR.
 */
static void syscall_restart_or_eintr(arch_syscall_frame_t *regs, int handler,
				     int sa_restart)
{
	int restart;

	switch ((int64_t)syscall_frame_return(regs))
	{
	case -ERESTARTSYS:
		restart = !handler || sa_restart;
		break;
	case -ERESTARTNOHAND:
		restart = !handler;
		break;
	case -ERESTARTNOINTR:
		restart = 1;
		break;
	default:
		return;
	}

	if (restart)
		syscall_frame_restart(regs);
	else
		syscall_frame_set_return(regs, (uint64_t)(int64_t)-EINTR);
}

/*
 * handle_signal - Make @p return to userspace into @sig's handler (Linux
 * handle_signal()). The interrupted state goes into the handler frame for
 * rt_sigreturn: @interrupted when the caller has the full user context
 * (rt_sigreturn), otherwise the syscall frame with the syscall's settled
 * result. The syscall frame then enters the handler. Returns 1 when the
 * handler runs, 0 when it does not (no frame could be built). Handlers
 * nest: a signal not blocked while a handler runs gets its own frame below
 * the running handler's.
 */
static int handle_signal(process_t *p, int sig,
			 const struct sigcontext *interrupted)
{
	arch_syscall_frame_t *regs = &p->syscall_frame;
	void (*handler)(int) = p->sighand->handlers[sig];
	uint32_t sa_flags = p->sighand->sa_flags[sig];
	struct signal_frame_setup setup;
	struct sigcontext ctx;

	if (!interrupted)
		syscall_restart_or_eintr(regs, 1, (sa_flags & SA_RESTART) != 0);

	if (interrupted)
		ctx = *interrupted;
	else
		signal_fill_sigcontext_from_syscall_frame(&ctx, regs,
							  syscall_frame_return(regs));

	if (signal_ip_is_registered_handler(p, sigcontext_ip(&ctx)))
	{
		if (signal_is_fault(sig))
			signal_exit_current(sig);
		p->signal_pending &= ~SIGNAL_MASK(sig);
		return 0;
	}

	if (signal_setup_frame(p, sig, &ctx, 0, 0, &setup) != SIGFRAME_OK)
	{
		/* Linux force_sigsegv(): no frame, no handler. */
		p->signal_pending &= ~SIGNAL_MASK(sig);
		signal_force_segv(p, sig);
		return 0;
	}

	syscall_frame_set_ip(regs, (uint64_t)(uintptr_t)handler);
	syscall_frame_set_sp(regs, setup.sp);
	syscall_frame_set_arg(regs, 0, (uint64_t)(uint32_t)sig);
	syscall_frame_set_arg(regs, 1, setup.info_addr);
	syscall_frame_set_arg(regs, 2, setup.uctx_addr);
	signal_set_return(regs, setup.restorer);

	p->signal_frame_sp = setup.sp;
	if (sa_flags & SA_RESETHAND)
		p->sighand->handlers[sig] = SIG_DFL;
	signals_apply_handler_mask(p, sig, sa_flags);
	p->restore_sigmask = 0;
	p->signal_pending &= ~SIGNAL_MASK(sig);
	return 1;
}

void set_current_blocked(uint64_t mask)
{
	current_process->signal_mask = mask & ~SIGNAL_UNBLOCKABLE;
}

void do_signal(process_t *p, const struct sigcontext *interrupted)
{
	int sig = signal_pending(p) ? get_signal(p) : 0;

	if (sig && handle_signal(p, sig, interrupted))
		return;

	if (!interrupted)
		syscall_restart_or_eintr(&p->syscall_frame, 0, 0);
	if (p->restore_sigmask)
	{
		p->signal_mask = p->saved_sigmask;
		p->restore_sigmask = 0;
	}
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
        if (!signal_code_addr_ok((const void *)handler))
            return -1; /* Invalid handler address */  
    }
    
    current->sighand->handlers[signal] = handler;
    process_sighand_note_ignore(current, signal, handler == SIG_IGN);
    
    return 0;
}

