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
#include <ir0/console.h>
#include <ir0/sched.h>
#include <ir0/clock.h>
#include <ir0/paging.h>
#include <ir0/kmem.h>
#include <ir0/signal_irq.h>
#include <ir0/sigtramp.h>
#include <ir0/fault.h>
#include <ir0/oops.h>
#include <kernel/process.h>
#include <string.h>
#include <ktm.h>
#include <config.h>

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
			      uint64_t fault_addr,
			      struct signal_frame_setup *out);

static int signal_rip_is_registered_handler(process_t *p, uint64_t rip)
{
	int i;

	if (!p || rip < 0x1000ul)
		return 0;
	for (i = 1; i < _NSIG; i++)
	{
		void (*h)(int) = p->signal_handlers[i];

		if (!h || h == SIG_DFL || h == SIG_IGN)
			continue;
		if ((uint64_t)(uintptr_t)h == rip)
			return 1;
	}
	return 0;
}

/* One handler frame at a time: a live one is ended by rt_sigreturn. */
static int signal_delivery_blocked_nested(process_t *p)
{
	return process_saved_context_present(p);
}

/*
 * The mask a handler frame saves (Linux sigmask_to_save()): the caller's own
 * mask while rt_sigsuspend runs with a temporary one.
 */
static uint32_t sigmask_to_save(const process_t *p)
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
	p->signal_mask |= p->signal_sa_mask[sig];
	if (!(sa_flags & SA_NODEFER))
		p->signal_mask |= SIGNAL_MASK(sig);
}

static void signals_restore_handler_mask(process_t *p)
{
	if (!p || !p->signal_mask_saved_valid)
		return;
	p->signal_mask = p->signal_mask_saved;
	p->signal_mask_saved_valid = 0;
}

void signals_on_sigreturn(process_t *p)
{
	signals_restore_handler_mask(p);
	if (p)
		p->signal_frame_sp = 0;
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

	klog_info_fmt("SIGNAL",
		      "SIGFRAME_ABANDON pid=%x usp=%llx frame_sp=%llx "
		      "mask_saved=%x",
		      (unsigned)((uint32_t)p->task.pid),
		      (unsigned long long)usp,
		      (unsigned long long)frame_sp,
		      (unsigned)(p->signal_mask_saved_valid ? 1U : 0U));

	process_saved_context_clear(p);
	/*
	 * Restore pre-delivery mask. Strict Linux leaves the handler mask
	 * after longjmp(); with a full sa_mask (ash sigfillset) that would
	 * mute the process. Restoring matches siglongjmp(... savesigs) and
	 * keeps the process signable.
	 *
	 * Userspace-first: stock BusyBox ash longjmps without rt_sigreturn;
	 * resync console input so GNU/BusyBox need no kernel-specific patches.
	 */
	signals_on_sigreturn(p);
	ir0_console_after_signal_abandon();
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
	handler = p->signal_handlers[sig];
	if (!handler || handler == SIG_DFL || handler == SIG_IGN)
		return 0;
	if (p->mode == USER_MODE &&
	    !mm_user_va_ok((uintptr_t)handler, sizeof(void *)))
		return 0;
	return 1;
}

int signals_deliver_from_irq_frame(process_t *p, int sig, uint64_t *frame,
				   uint64_t fault_addr)
{
	void (*handler)(int);
	struct sigcontext *ctx;
	struct signal_frame_setup setup;
	uint32_t sa_flags;

	if (!signals_has_user_handler(p, sig))
		return 0;
	if (!frame || p->mode != USER_MODE)
		return 0;

	/*
	 * Already in a userspace handler: a nested SEGV must not replace the
	 * outer saved context (that dropped the interrupted setjmp site and
	 * left rdi=signum on resume). Treat nested fault as fatal.
	 */
	if (signal_delivery_blocked_nested(p))
	{
		if (sig == SIGSEGV || sig == SIGBUS || sig == SIGILL ||
		    sig == SIGFPE)
		{
			signal_exit_current(sig);
		}
		return 0;
	}

	handler = p->signal_handlers[sig];
	sa_flags = p->signal_sa_flags[sig];

	ctx = kmalloc(sizeof(*ctx));
	if (!ctx)
		return 0;

	memset(ctx, 0, sizeof(*ctx));
	signal_fill_sigcontext_from_irq_frame(ctx, frame);
	if (signal_rip_is_registered_handler(p, sigcontext_ip(ctx)))
	{
		kfree(ctx);
		if (sig == SIGSEGV || sig == SIGBUS || sig == SIGILL ||
		    sig == SIGFPE)
		{
			signal_exit_current(sig);
		}
		return 0;
	}
	if (process_saved_context_attach(p, ctx) != 0)
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
	if (signal_setup_frame(p, sig, ctx, fault_addr, &setup) != SIGFRAME_OK)
	{
		process_saved_context_clear(p);
		signal_exit_current(SIGSEGV);
	}

	signal_redirect_irq_frame(frame, (void *)handler, sig, setup.sp,
				  setup.info_addr, setup.uctx_addr,
				  (sa_flags & SA_SIGINFO) ? 1 : 0,
				  setup.restorer);
	process_save_user_exception_frame(frame);

	if (sa_flags & SA_RESETHAND)
		p->signal_handlers[sig] = SIG_DFL;

	p->signal_frame_sp = setup.sp;
	signals_apply_handler_mask(p, sig, sa_flags);
	p->signal_pending &= ~SIGNAL_MASK(sig);

#if SIGNAL_DELIVER_LOG
	klog_info_fmt("SIGNAL",
		      "[SIGNAL][DELIVER] pid=0x%x sig=0x%x cr2=0x%llx rip=0x%llx handler=0x%llx sa_siginfo=0x%x rsp=0x%llx",
		      (unsigned)p->task.pid, (unsigned)sig,
		      (unsigned long long)fault_addr,
		      (unsigned long long)sigcontext_ip(ctx),
		      (unsigned long long)(uintptr_t)handler,
		      (unsigned)((sa_flags & SA_SIGINFO) ? 1U : 0U),
		      (unsigned long long)setup.sp);
#endif

#if defined(CONFIG_KTM_FLIGHT) && CONFIG_KTM_FLIGHT
	{
		uint32_t pid = (uint32_t)p->task.pid;

		KTM_FLIGHT(KTM_FL_PF_USER, pid, (uint32_t)fault_addr,
			   (uint32_t)(fault_addr >> 32),
			   (uint32_t)sigcontext_ip(ctx));
		KTM_FLIGHT(KTM_FL_SIGNAL_DELIVER, (uint32_t)sig, pid,
			   (uint32_t)(uintptr_t)handler, 0);
	}
#endif

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
		return signals_deliver_from_irq_frame(p, sig, irq_frame,
						      fault ? fault->addr : 0);
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
	process_saved_context_clear(p);
	p->signal_pending = 0;
	p->signal_mask = 0;
	p->signal_ignored = 0;
	p->signal_mask_saved = 0;
	p->signal_mask_saved_valid = 0;
	p->signal_frame_sp = 0;
	p->it_real_expire_ms = 0;
	p->it_real_interval_ms = 0;
	for (i = 0; i < _NSIG; i++)
	{
		p->signal_handlers[i] = SIG_DFL;
		p->signal_sa_flags[i] = 0;
		p->signal_sa_mask[i] = 0;
		p->signal_restorer[i] = NULL;
	}
}

/*
 * signal_pending - @p has a signal that must end an interruptible sleep
 * (Linux signal_pending()): pending and not blocked, or SIGKILL. Ignored
 * signals do not count, nor do those whose default action is to ignore
 * them (SIGCHLD, SIGWINCH, SIGCONT, SIGURG) while they have no handler.
 * While a handler frame is live, caught signals wait for its rt_sigreturn.
 */
int signal_pending(process_t *p)
{
	uint32_t deliverable;
	int sig;

	if (!p || p->signal_pending == 0)
		return 0;
	if (p->signal_pending & SIGNAL_MASK(SIGKILL))
		return 1;

	deliverable = p->signal_pending & ~p->signal_mask & ~p->signal_ignored;
	for (sig = 1; sig < _NSIG && deliverable; sig++)
	{
		void (*handler)(int) = p->signal_handlers[sig];
		int caught = handler && handler != SIG_DFL && handler != SIG_IGN;

		if (!(deliverable & SIGNAL_MASK(sig)))
			continue;
		if (caught ? signal_delivery_blocked_nested(p)
			   : (sig == SIGCHLD || sig == SIGWINCH ||
			      sig == SIGCONT || sig == SIGURG))
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

#if IR0_DEBUG_PROC
    klog_info_fmt("SIGNAL",
                  "[SIGTERM_AUDIT] send_signal pid=0x%x sig=0x%x pending=0x%x mask=0x%x state=0x%x",
                  (unsigned)pid, (unsigned)signal,
                  (unsigned)proc->signal_pending, (unsigned)proc->signal_mask,
                  (unsigned)proc->state);
#endif

    /*
     * Linux: signals to PID 1 are discarded unless init installed a handler
     * (man 2 kill). BusyBox poweroff/reboot without -f send SIGUSR2/SIGTERM
     * to PID 1; runit has no handler — leaving them pending destabilized
     * stage supervision and later showed up as kernel #UD.
     */
    if (proc->task.pid == 1)
    {
	void (*handler)(int) = proc->signal_handlers[signal];

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
#if IR0_DEBUG_PROC
	klog_info_fmt("SIGNAL",
		      "[SIGTERM_AUDIT] wake pid=0x%x state=READY promote=1",
		      (unsigned)pid);
#endif
    }

#if DEBUG_PROCESS
    klog_info("SIGNAL", "Sent signal to process");
#endif

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
 * Pick a mapped user stack slot for [return slot][sigframe] + handler redzone.
 * The return slot holds the restorer on ISAs whose handlers return with a
 * stack pop (SIGFRAME_RETURN_SLOT); it is empty on link-register ISAs.
 * When the interrupted SP is near the stack guard (deep call chain in
 * recvfrom → ping), placing the frame below saved SP lands in unmapped
 * pages and syscalls from the handler (clock_gettime in SIGALRM) fail with
 * -EFAULT.
 *
 * Stale syscall_frame SP (outside the process stack VMA) must not be used —
 * BusyBox ping after exec can interrupt with bogus sp≈0xffa… and unmapped
 * handler locals.
 */
static int signal_sp_in_stack(process_t *p, uint64_t sp)
{
	uint64_t lo;
	uint64_t hi;

	if (!p || sp == 0)
		return 0;

	lo = process_stack_start(p);
	hi = lo + process_stack_size(p);
	if (lo == 0 || hi <= lo)
	{
		lo = USER_STACK_BASE;
		hi = USER_STACK_TOP;
	}

	return sp >= lo + 16 && sp < hi;
}

static uint64_t signal_pick_handler_sp(process_t *p, uint64_t saved_sp,
				       uint64_t *frame_addr_out)
{
	uint64_t stack_lo;
	uint64_t stack_hi;
	uint64_t user_sp;
	uint64_t frame_addr;
	uint64_t min_sp;

	if (!p || !frame_addr_out)
		return 0;

	stack_lo = process_stack_start(p);
	stack_hi = stack_lo + process_stack_size(p);
	if (stack_lo == 0 || stack_hi <= stack_lo)
	{
		stack_lo = USER_STACK_BASE;
		stack_hi = USER_STACK_TOP;
	}

	saved_sp &= ~0xFUL;
	min_sp = stack_lo + 2048UL;

	if (!signal_sp_in_stack(p, saved_sp))
		saved_sp = 0;

	user_sp = saved_sp;
	if (user_sp < sizeof(struct sigframe) + SIGFRAME_RETURN_SLOT)
		user_sp = 0;

	/*
	 * Deep call chain left SP near USER_STACK_TOP: building the frame below
	 * saved SP still runs the handler in the canary band (STACK_TOP_OVERRUN).
	 */
	if (user_sp != 0 && user_sp > stack_hi - SIGNAL_HANDLER_TOP_MARGIN)
		user_sp = 0;

	if (user_sp == 0 || user_sp < min_sp)
	{
		uint64_t margin = SIGNAL_HANDLER_TOP_MARGIN;

		if (margin + sizeof(struct sigframe) + SIGFRAME_RETURN_SLOT +
		    2048UL > (stack_hi - stack_lo))
			margin = (stack_hi - stack_lo) / 4;

		user_sp = (stack_hi - margin) & ~0xFUL;
		if (user_sp < sizeof(struct sigframe) + SIGFRAME_RETURN_SLOT +
			      min_sp)
			return 0;
	}
	else
	{
		if (user_sp < sizeof(struct sigframe) + SIGFRAME_RETURN_SLOT)
			return 0;
	}

	user_sp -= sizeof(struct sigframe);
	frame_addr = user_sp;
	user_sp -= SIGFRAME_RETURN_SLOT;

	if (user_sp < min_sp || frame_addr + sizeof(struct sigframe) > stack_hi)
		return 0;
	if (user_sp < 0x400000UL || user_sp > 0x7FFFFFFFFFFFUL)
		return 0;

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
	void (*fn)(void) = p->signal_restorer[sig];

	if (!fn)
		return sigtramp_addr();
	if (!mm_user_va_ok((uintptr_t)fn, sizeof(void *)))
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
			      uint64_t fault_addr,
			      struct signal_frame_setup *out)
{
	struct sigframe frame;
	uint64_t frame_addr;
	uint64_t user_sp;
	uint64_t restorer;

	user_sp = signal_pick_handler_sp(p, sigcontext_sp(ctx), &frame_addr);
	if (user_sp == 0)
		return SIGFRAME_BAD_SP;
	restorer = signal_handler_restorer(p, sig);
	if (restorer == 0)
		return SIGFRAME_NO_RESTORER;

	memset(&frame, 0, sizeof(frame));
	frame.handler = p->signal_handlers[sig];
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
	if (fault_addr != 0)
	{
		frame.info.si_code = SEGV_MAPERR;
		frame.info._sifields._sigfault.si_addr =
			(void *)(uintptr_t)fault_addr;
	}

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
 * SIGKILL and an uncaught default-terminate signal end the task here;
 * SIGSTOP sleeps until SIGCONT. Ignored signals are dropped and blocked
 * ones stay pending. BusyBox ash relies on this: it catches SIGINT and may
 * raise() it after a foreground job, and a blocked SIGTERM must not kill it.
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
		{
			/*
			 * Pending-bit kill without a live fault frame (kill(2)
			 * from another path): greppable tag for session smokes.
			 */
			klog_info_fmt("FAULT",
				      "USER_FAULT_FRAME CLASSIFY SIGNAL_KILL_SIGSEGV "
				      "pid=%x comm=%s",
				      (unsigned)((uint32_t)p->task.pid),
				      p->comm[0] ? p->comm : "(none)");
			klog_print("USER_FAULT_FRAME\n");
		}
		signal_exit_current(sig);
	}

	/* SIGCONT already woke a stopped task when it was sent. */
	p->signal_pending &= ~SIGNAL_MASK(SIGCONT);

	for (sig = 1; sig < _NSIG; sig++)
	{
		uint32_t bit = SIGNAL_MASK(sig);
		void (*handler)(int) = p->signal_handlers[sig];

		if (!(p->signal_pending & bit))
			continue;
		if (p->signal_ignored & bit)
		{
			p->signal_pending &= ~bit;
			continue;
		}
		if ((p->signal_mask & bit) || !handler || handler == SIG_DFL ||
		    handler == SIG_IGN)
			continue;
		if (!mm_user_va_ok((uintptr_t)handler, sizeof(void *)))
		{
			/* A handler outside userspace falls back to the default. */
			p->signal_handlers[sig] = SIG_DFL;
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
 * handle_signal - Make @p return from its syscall into @sig's handler
 * (Linux handle_signal()). The interrupted state, with the syscall's
 * settled result, goes into the handler frame for rt_sigreturn, and the
 * syscall frame enters the handler. Returns 1 when the handler runs, 0
 * when the signal stays pending (a handler frame is already live).
 */
static int handle_signal(process_t *p, int sig)
{
	arch_syscall_frame_t *regs = &p->syscall_frame;
	void (*handler)(int) = p->signal_handlers[sig];
	uint32_t sa_flags = p->signal_sa_flags[sig];
	struct signal_frame_setup setup;
	struct sigcontext *ctx;

	/*
	 * Nested delivery while a handler frame is live is refused until
	 * rt_sigreturn: a nested synchronous fault is fatal, anything else
	 * waits pending.
	 */
	if (signal_delivery_blocked_nested(p))
	{
		if (signal_is_fault(sig))
			signal_exit_current(sig);
		return 0;
	}

	syscall_restart_or_eintr(regs, 1, (sa_flags & SA_RESTART) != 0);

	ctx = kmalloc(sizeof(*ctx));
	if (!ctx)
	{
		p->signal_handlers[sig] = SIG_DFL;
		p->signal_pending &= ~SIGNAL_MASK(sig);
		return 0;
	}
	signal_fill_sigcontext_from_syscall_frame(ctx, regs,
						  syscall_frame_return(regs));

	if (signal_rip_is_registered_handler(p, sigcontext_ip(ctx)) ||
	    process_saved_context_attach(p, ctx) != 0)
	{
		kfree(ctx);
		if (signal_is_fault(sig))
			signal_exit_current(sig);
		p->signal_pending &= ~SIGNAL_MASK(sig);
		return 0;
	}

	switch (signal_setup_frame(p, sig, ctx, 0, &setup))
	{
	case SIGFRAME_OK:
		break;
	case SIGFRAME_NO_RESTORER:
		process_saved_context_clear(p);
		p->signal_pending &= ~SIGNAL_MASK(sig);
		signal_force_segv(p, sig);
		return 0;
	default:
		/* Linux force_sigsegv(): no frame, no handler. */
		process_saved_context_clear(p);
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
		p->signal_handlers[sig] = SIG_DFL;
	signals_apply_handler_mask(p, sig, sa_flags);
	p->restore_sigmask = 0;
	p->signal_pending &= ~SIGNAL_MASK(sig);
	if (sig == SIGCHLD)
		ir0_console_after_tty_read_signal(SIGCHLD);
	return 1;
}

void do_signal(process_t *p)
{
	int sig = signal_pending(p) ? get_signal(p) : 0;

	if (sig && handle_signal(p, sig))
		return;

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
        if (!mm_user_va_ok((uintptr_t)handler, sizeof(void *)))
            return -1; /* Invalid handler address */  
    }
    
    current->signal_handlers[signal] = handler;
    
    /* If handler is SIG_IGN, add to ignored mask */
    if (handler == SIG_IGN)
    {
        current->signal_ignored |= SIGNAL_MASK(signal);
        current->signal_pending &= ~SIGNAL_MASK(signal);
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
