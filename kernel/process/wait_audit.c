/** IR0 wait/exit diagnostics kept outside the functional wait path. */
/* SPDX-License-Identifier: GPL-3.0-only */

#include "process_internal.h"

void wait_exit_audit_classify_user_frame(const char *tag, process_t *p)
{
#if !IR0_DEBUG_WAIT
	(void)tag;
	(void)p;
#else
	uint64_t ip;
	uint64_t sp;

	if (!p)
		return;
	ip = task_get_ip(&p->task);
	sp = task_get_sp(&p->task);
	klog_debug_fmt("WAIT", "[WAIT_EXIT_AUDIT][FRAME] tag=%s pid=%x comm=%s ip=%llx sp=%llx return_user=%llx status=%llx retval=%llx address_space=%llx irq_saved=%llx", tag ? tag : "(null)", (unsigned)((uint32_t)p->task.pid), p->comm, (unsigned long long)ip, (unsigned long long)sp, (unsigned long long)task_return_state_is_user(&p->task), (unsigned long long)task_get_flags(&p->task), (unsigned long long)task_get_retval(&p->task), (unsigned long long)process_mm_root(p), (unsigned long long)p->irq_frame_saved);
	if (!mm_user_va_ok((uintptr_t)ip, 1))
		klog_debug("WAIT", "CLASSIFY PARENT_IRET_FRAME_BAD_RIP");
	if (!mm_user_va_ok((uintptr_t)sp, 1))
		klog_debug("WAIT", "CLASSIFY PARENT_IRET_FRAME_BAD_RSP");
	if (!task_return_state_is_user(&p->task))
		klog_debug("WAIT", "CLASSIFY PARENT_RETURN_STATE_NOT_USER");
#endif
}

void wait_exit_audit_process_exit(process_t *dying, process_t *parent,
				  int parent_state_before)
{
#if !IR0_DEBUG_WAIT
	(void)dying;
	(void)parent;
	(void)parent_state_before;
#else
	klog_debug_fmt("WAIT", "[WAIT_EXIT_AUDIT][process_exit] child_pid=%x child_state_before=RUNNING child_state_after=ZOMBIE parent_pid=%x parent_state_before=%llx parent_state_after=%llx parent_woken=%llx", (unsigned)((uint32_t)dying->task.pid), (unsigned)(parent ? (uint32_t)parent->task.pid : 0), (unsigned long long)(uint32_t)parent_state_before, (unsigned long long)(parent ? (uint32_t)parent->state : 0), (unsigned long long)(parent && parent_state_before == PROCESS_BLOCKED && parent->state == PROCESS_READY));
	klog_debug("WAIT", "[WAIT_EXIT_AUDIT][process_exit] child retains task/MM until reap");
	if (parent)
		wait_exit_audit_classify_user_frame("parent-at-child-exit", parent);
	if (dying->irq_frame_saved)
		klog_debug("WAIT", "CLASSIFY EXIT_FREED_PARENT_RESOURCE note=child_irq_saved_stale_may_misroute_resume");
#endif
}

void wait_exit_audit_process_wait_block(pid_t wait_pid, int *status)
{
#if !IR0_DEBUG_WAIT
	(void)wait_pid;
	(void)status;
#else
	if (!current_process)
		return;
	klog_debug_fmt("WAIT", "[WAIT_EXIT_AUDIT][process_wait] action=block parent_pid=%x wait_pid=%x status_ptr=%llx", (unsigned)((uint32_t)current_process->task.pid), (unsigned)((uint32_t)wait_pid), (unsigned long long)(uintptr_t)status);
	wait_exit_audit_classify_user_frame("parent-before-wait-sleep", current_process);
#endif
}

void wait_exit_audit_process_wait_reap(pid_t reaped_pid, int status_val,
				       int *status)
{
#if !IR0_DEBUG_WAIT
	(void)reaped_pid;
	(void)status_val;
	(void)status;
#else
	if (!current_process)
		return;
	klog_debug_fmt("WAIT", "[WAIT_EXIT_AUDIT][process_wait] action=reap parent_pid=%x reaped_pid=%x status_val=%llx status_ptr=%llx", (unsigned)((uint32_t)current_process->task.pid), (unsigned)((uint32_t)reaped_pid), (unsigned long long)(uint32_t)status_val, (unsigned long long)(uintptr_t)status);
	wait_exit_audit_classify_user_frame("parent-after-reap", current_process);
#endif
}
