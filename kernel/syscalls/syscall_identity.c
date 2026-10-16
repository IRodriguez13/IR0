/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Process-identity syscall owners. Linux reads task_tgid_vnr(current)
 * and cred->uid; IR0 reads current_process.
 */

#include <ir0/errno.h>
#include <ir0/process.h>
#include <ir0/syscall_identity.h>

static int64_t identity_getpid(void)
{
	if (!current_process)
		return -ESRCH;
	return (int64_t)process_get_pid();
}

static int64_t identity_gettid(void)
{
	if (!current_process)
		return -ESRCH;
	return (int64_t)process_get_pid();
}

static int64_t identity_getppid(void)
{
	if (!current_process)
		return -ESRCH;
	return (int64_t)process_get_ppid();
}

static int64_t identity_getuid(void)
{
	if (!current_process)
		return -ESRCH;
	return (int64_t)current_process->uid;
}

static int64_t identity_geteuid(void)
{
	if (!current_process)
		return -ESRCH;
	return (int64_t)current_process->euid;
}

static int64_t identity_getgid(void)
{
	if (!current_process)
		return -ESRCH;
	return (int64_t)current_process->gid;
}

static int64_t identity_getegid(void)
{
	if (!current_process)
		return -ESRCH;
	return (int64_t)current_process->egid;
}

static void identity_sync_current(int64_t pid)
{
	process_t *p;

	if (pid <= 0)
		return;
	p = process_find_by_pid((pid_t)pid);
	if (p)
		current_process = p;
}

static void identity_publish_creds(uint32_t uid, uint32_t euid,
				   uint32_t gid, uint32_t egid)
{
	if (!current_process)
		return;
	current_process->uid = uid;
	current_process->euid = euid;
	current_process->gid = gid;
	current_process->egid = egid;
}

static const struct ir0_syscall_identity_ops g_identity_ops = {
	.getpid = identity_getpid,
	.gettid = identity_gettid,
	.getppid = identity_getppid,
	.getuid = identity_getuid,
	.geteuid = identity_geteuid,
	.getgid = identity_getgid,
	.getegid = identity_getegid,
	.sync_current = identity_sync_current,
	.publish_creds = identity_publish_creds,
};

const struct ir0_syscall_identity_ops *ir0_syscall_identity_product(void)
{
	return &g_identity_ops;
}
