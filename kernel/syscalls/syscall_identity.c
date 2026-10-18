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
#include <ir0/syscall_id.h>
#include <ir0/syscall_identity.h>
#include <ir0/syscall_table.h>

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
		process_set_current(p);
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

static int64_t product_identity_syscall(void *context, enum ir0_syscall_id id,
					uint64_t a0, uint64_t a1, uint64_t a2,
					uint64_t a3, uint64_t a4, uint64_t a5)
{
	(void)context;
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
	(void)a4;
	(void)a5;
	switch (id)
	{
	case IR0_SYSCALL_GETPID:
		return identity_getpid();
	case IR0_SYSCALL_GETTID:
		return identity_gettid();
	case IR0_SYSCALL_GETPPID:
		return identity_getppid();
	case IR0_SYSCALL_GETUID:
		return identity_getuid();
	case IR0_SYSCALL_GETEUID:
		return identity_geteuid();
	case IR0_SYSCALL_GETGID:
		return identity_getgid();
	case IR0_SYSCALL_GETEGID:
		return identity_getegid();
	default:
		return -ENOSYS;
	}
}

static const enum ir0_syscall_id g_product_identity_ids[] = {
	IR0_SYSCALL_GETPID, IR0_SYSCALL_GETTID,
	IR0_SYSCALL_GETUID, IR0_SYSCALL_GETEUID, IR0_SYSCALL_GETGID,
	IR0_SYSCALL_GETEGID, IR0_SYSCALL_GETPPID,
};

const struct syscall_context_provider ir0_product_identity_provider = {
	.ids = g_product_identity_ids,
	.count = sizeof(g_product_identity_ids) / sizeof(g_product_identity_ids[0]),
	.handler = product_identity_syscall,
};
