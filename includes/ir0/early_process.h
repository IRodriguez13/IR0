/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <stdint.h>
#include <ir0/clone.h>
#include <ir0/syscall_identity.h>
#include <ir0/syscall_table.h>

/*
 * Staged EL0 is PID 1: session leader and process-group leader.
 * Linux setsid(2) fails with EPERM when the caller is already a
 * process-group leader (man7.org/linux/man-pages/man2/setsid.2.html).
 * getpgid(2)/getsid(2) return ESRCH when the pid does not exist.
 */
#define IR0_EARLY_EPERM 1
#define IR0_EARLY_ESRCH 3
#define IR0_EARLY_EINVAL 22
#define IR0_EARLY_ENOSYS 38
#define IR0_EARLY_CSIGNAL 0xffUL
#define IR0_EARLY_SIGCHLD 17
#define IR0_EARLY_EL0_FRAME_WORDS 34

/*
 * Linux clone(2) on AArch64 is syscall 220:
 *   clone(flags, stack, parent_tid, tls, child_tid)
 * (man7.org/linux/man-pages/man2/clone.2.html; musl
 * src/thread/aarch64/clone.s). CSIGNAL is the low 8 bits. Staged
 * EL0 implements a real child only with CLONE_VM (shared address
 * space). Private mm / COW fork stays ENOSYS.
 */
#define IR0_EARLY_CLONE_ALLOWED \
	(CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_PIDFD | \
	 CLONE_VFORK | CLONE_SETTLS | CLONE_PARENT_SETTID | \
	 CLONE_CHILD_CLEARTID | CLONE_CHILD_SETTID)
#define IR0_EARLY_CLONE_ARGS_VER0 64U
#define IR0_EARLY_CLONE_ARGS_VER2 88U
#define IR0_EARLY_CLONE_NEWTIME 0x00000080UL
#define IR0_EARLY_CLONE_NEWNS 0x00020000UL
#define IR0_EARLY_CLONE_NEWCGROUP 0x02000000UL
#define IR0_EARLY_CLONE_NEWUTS 0x04000000UL
#define IR0_EARLY_CLONE_NEWIPC 0x08000000UL
#define IR0_EARLY_CLONE_NEWUSER 0x10000000UL
#define IR0_EARLY_CLONE_NEWPID 0x20000000UL
#define IR0_EARLY_CLONE_NEWNET 0x40000000UL
#define IR0_EARLY_CLONE_NS_ALL \
	(IR0_EARLY_CLONE_NEWTIME | IR0_EARLY_CLONE_NEWNS | \
	 IR0_EARLY_CLONE_NEWCGROUP | IR0_EARLY_CLONE_NEWUTS | \
	 IR0_EARLY_CLONE_NEWIPC | IR0_EARLY_CLONE_NEWUSER | \
	 IR0_EARLY_CLONE_NEWPID | IR0_EARLY_CLONE_NEWNET)

static inline int ir0_early_clone_flags_ok(uint64_t flags)
{
	uint64_t extras = flags & ~IR0_EARLY_CSIGNAL;

	if ((extras & CLONE_VM) == 0)
		return 0;
	if (extras & CLONE_THREAD)
		return 0;
	if (extras & ~IR0_EARLY_CLONE_ALLOWED)
		return 0;
	return 1;
}

static inline int64_t ir0_early_process_getpgid(uint64_t pid)
{
	if (pid == 0 || pid == 1)
		return 1;
	return -IR0_EARLY_ESRCH;
}

static inline int64_t ir0_early_process_getsid(uint64_t pid)
{
	return ir0_early_process_getpgid(pid);
}

static inline int64_t ir0_early_process_setsid(void)
{
	return -IR0_EARLY_EPERM;
}

static inline int64_t ir0_early_process_setpgid(uint64_t pid, uint64_t pgid)
{
	if (pid != 0 && pid != 1)
		return -IR0_EARLY_ESRCH;
	(void)pgid;
	return -IR0_EARLY_EPERM;
}

/*
 * Linux kill(2): EINVAL if sig is outside 0..NSIG; ESRCH if the pid
 * does not exist. pid 0 is the caller's process group (PID 1 here).
 * pid -1 means "every process except init"; staged EL0 has no other
 * tasks, so that is ESRCH.
 */
static inline int64_t ir0_early_process_kill(int64_t pid, uint64_t sig)
{
	if (sig > 64)
		return -IR0_EARLY_EINVAL;
	if (pid == 0 || pid == 1)
		return 0;
	return -IR0_EARLY_ESRCH;
}

static inline int64_t ir0_early_process_tkill(int64_t tid, uint64_t sig)
{
	if (sig > 64)
		return -IR0_EARLY_EINVAL;
	if (tid == 1)
		return 0;
	return -IR0_EARLY_ESRCH;
}

static inline int64_t ir0_early_process_tgkill(int64_t tgid, int64_t tid,
					       uint64_t sig)
{
	if (sig > 64)
		return -IR0_EARLY_EINVAL;
	if (tgid != 0 && tgid != 1)
		return -IR0_EARLY_ESRCH;
	return ir0_early_process_tkill(tid, sig);
}

#define IR0_EARLY_SVC_INSN_BYTES 4

/*
 * Linux include/uapi/linux/prctl.h and sched.h. TASK_COMM_LEN is 16.
 * Unknown prctl options return EINVAL. Realtime SCHED_FIFO/RR is EPERM
 * (no RT class). membarrier(2) QUERY returns the supported cmd mask.
 */
#define IR0_EARLY_TASK_COMM_LEN 16
#define IR0_EARLY_PR_SET_PDEATHSIG 1
#define IR0_EARLY_PR_GET_PDEATHSIG 2
#define IR0_EARLY_PR_GET_DUMPABLE 3
#define IR0_EARLY_PR_SET_DUMPABLE 4
#define IR0_EARLY_PR_GET_KEEPCAPS 7
#define IR0_EARLY_PR_SET_KEEPCAPS 8
#define IR0_EARLY_PR_SET_NAME 15
#define IR0_EARLY_PR_GET_NAME 16
#define IR0_EARLY_PR_GET_SECCOMP 21
#define IR0_EARLY_PR_CAPBSET_READ 23
#define IR0_EARLY_PR_CAPBSET_DROP 24
#define IR0_EARLY_PR_SET_NO_NEW_PRIVS 38
#define IR0_EARLY_PR_GET_NO_NEW_PRIVS 39
#define IR0_EARLY_PR_GET_TID_ADDRESS 40
#define IR0_EARLY_PR_SET_THP_DISABLE 41
#define IR0_EARLY_PR_GET_THP_DISABLE 42
#define IR0_EARLY_PR_SET_PTRACER 0x59616d61UL
#define IR0_EARLY_PR_SET_VMA 0x53564d41UL
#define IR0_EARLY_PR_SET_VMA_ANON_NAME 0
#define IR0_EARLY_SCHED_OTHER 0
#define IR0_EARLY_SCHED_FIFO 1
#define IR0_EARLY_SCHED_RR 2
#define IR0_EARLY_SCHED_BATCH 3
#define IR0_EARLY_SCHED_IDLE 5
#define IR0_EARLY_MEMBARRIER_CMD_QUERY 0
#define IR0_EARLY_MEMBARRIER_CMD_GLOBAL 1
#define IR0_EARLY_MEMBARRIER_CMD_GLOBAL_EXPEDITED 2
#define IR0_EARLY_MEMBARRIER_CMD_REGISTER_GLOBAL_EXPEDITED 4
#define IR0_EARLY_MEMBARRIER_CMD_PRIVATE_EXPEDITED 8
#define IR0_EARLY_MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED 16
#define IR0_EARLY_MEMBARRIER_SUPPORTED \
	(IR0_EARLY_MEMBARRIER_CMD_GLOBAL | \
	 IR0_EARLY_MEMBARRIER_CMD_GLOBAL_EXPEDITED | \
	 IR0_EARLY_MEMBARRIER_CMD_REGISTER_GLOBAL_EXPEDITED | \
	 IR0_EARLY_MEMBARRIER_CMD_PRIVATE_EXPEDITED | \
	 IR0_EARLY_MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED)
#define IR0_EARLY_IOV_MAX 1024
#define IR0_EARLY_IOV_BYTES 16
#define IR0_EARLY_LINUX_CAP_VERSION_3 0x20080522U
#define IR0_EARLY_CAP_U32S 2
#define IR0_EARLY_CAP_HEADER_BYTES 8
#define IR0_EARLY_CAP_DATA_BYTES 24
#define IR0_EARLY_IOPRIO_WHO_PROCESS 1
#define IR0_EARLY_IOPRIO_WHO_PGRP 2
#define IR0_EARLY_IOPRIO_WHO_USER 3
#define IR0_EARLY_IOPRIO_CLASS_SHIFT 13
#define IR0_EARLY_IOPRIO_CLASS_NONE 0
#define IR0_EARLY_IOPRIO_CLASS_RT 1
#define IR0_EARLY_IOPRIO_CLASS_BE 2
#define IR0_EARLY_IOPRIO_CLASS_IDLE 3
#define IR0_EARLY_SCHED_ATTR_SIZE_VER0 48U
#define IR0_EARLY_SCHED_ATTR_SIZE_VER1 56U
#define IR0_EARLY_KCMP_FILE 0
#define IR0_EARLY_KCMP_VM 1
#define IR0_EARLY_KCMP_FILES 2
#define IR0_EARLY_KCMP_FS 3
#define IR0_EARLY_KCMP_SIGHAND 4
#define IR0_EARLY_KCMP_IO 5
#define IR0_EARLY_KCMP_SYSVSEM 6
#define IR0_EARLY_PIDFD_NONBLOCK 00004000U
#define IR0_EARLY_HOSTNAME_MAX 64
#define IR0_EARLY_FUTEX2_SIZE_U32 0x02U
#define IR0_EARLY_FUTEX2_NUMA 0x04U
#define IR0_EARLY_FUTEX2_MPOL 0x08U
#define IR0_EARLY_FUTEX2_PRIVATE 0x80U
#define IR0_EARLY_FUTEX2_SIZE_MASK 0x03U
#define IR0_EARLY_FUTEX2_VALID_MASK \
	(IR0_EARLY_FUTEX2_SIZE_MASK | IR0_EARLY_FUTEX2_NUMA | \
	 IR0_EARLY_FUTEX2_MPOL | IR0_EARLY_FUTEX2_PRIVATE)
#define IR0_EARLY_FUTEX_WAITV_MAX 128U
#define IR0_EARLY_FUTEX_WAITV_BYTES 24U

static inline int ir0_early_sched_policy_ok(int policy)
{
	return policy == IR0_EARLY_SCHED_OTHER ||
	       policy == IR0_EARLY_SCHED_BATCH ||
	       policy == IR0_EARLY_SCHED_IDLE;
}

static inline int ir0_early_sched_policy_rt(int policy)
{
	return policy == IR0_EARLY_SCHED_FIFO || policy == IR0_EARLY_SCHED_RR;
}

extern const struct syscall_context_provider early_process_provider;
int early_process_smoke_ok(void);
int early_process_is_root(void);
int early_process_clone_take_enter(uint64_t *stack, uint64_t *tls);
void early_process_clone_save_parent(const uint64_t *frame);
int early_process_clone_resume_parent(uint64_t *frame);
int early_process_request_yield(void);
int early_process_take_yield(uint64_t *frame);
int early_process_block_wait_observed(void);
int64_t early_process_current_pid(void);
void early_process_current_ucred(int32_t *pid, int32_t *uid, int32_t *gid);
void early_process_note_exec(void);
int early_process_vfork_exec_resume(uint64_t *frame);
void early_process_bind_common_exit(int (*fn)(int code));
void early_process_bind_schedule_exit(void (*fn)(int code));
void early_process_arm_schedule_exit(void);
int early_process_common_exit_published(void);
void early_process_bind_identity(const struct ir0_syscall_identity_ops *ops);
int early_process_identity_used(void);
struct ir0_fd_install_ops;
void early_process_bind_files(const struct ir0_fd_install_ops *ops);
