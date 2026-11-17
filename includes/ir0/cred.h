/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: cred.h
 * Description: Task credentials and the DAC permission check (Linux
 *              <linux/cred.h>). kernel/process/credentials.c owns the
 *              credentials, fs/permissions.c the permission check.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <ir0/errno.h>
#include <ir0/types.h>
#include <ir0/stat.h>

/*
 * Well-known IDs used by the kernel itself (root checks) and by tests.
 * Account data lives in userspace: /etc/passwd, /etc/shadow, /etc/group.
 */
#define ROOT_UID     0
#define ROOT_GID     0
#define USER_UID     1000
#define USER_GID     1000

/* Default umask (022 = ----w--w-) */
#define DEFAULT_UMASK 0022

/* Access modes for generic_permission() */
#define ACCESS_READ    1
#define ACCESS_WRITE   2
#define ACCESS_EXEC    4

struct ir0_task_cred
{
    uid_t uid;
    gid_t gid;
    uid_t euid;
    gid_t egid;
    mode_t umask;
    pid_t pid;

    /*
     * Points at the current process cwd buffer (nul-terminated) while the
     * caller holds the scheduler lock / runs in syscall context.
     */
    const char *cwd;

    /* Supplementary groups (setgroups(2)); they count for DAC like egid. */
    const gid_t *groups;
    int ngroups;
};

const struct ir0_task_cred *ir0_current_cred(void);
pid_t ir0_current_pid(void);
int ir0_cred_is_root(void);

/*
 * generic_permission - may @cred access the object @st describes with
 * @mask (ACCESS_*)? Linux fs/namei.c rules: the owner bits for the owner,
 * the group bits for a member of the file group (effective or
 * supplementary), else the other bits; root may read and write anything
 * and search any directory, but execute a file only when it has an exec
 * bit. Returns 0 or -EACCES (also for a NULL argument). Implemented in fs/permissions.c; the caller looks
 * the object up (vfs_stat(), ir0_stat_path_routed()) and passes
 * ir0_current_cred(), or a copy with the real IDs for access(2).
 */
int generic_permission(const struct ir0_task_cred *cred, const stat_t *st,
                       int mask);

typedef struct ir0_cred_id_triplet
{
	uint32_t real;
	uint32_t effective;
	uint32_t saved;
} ir0_cred_id_triplet_t;

/*
 * Linux kernel/sys.c __sys_setreuid()/__sys_setregid() semantics. Validate
 * before mutation so a denied transition cannot partially change credentials.
 */
static inline int ir0_cred_setreid(ir0_cred_id_triplet_t *ids,
				   uint32_t requested_real,
				   uint32_t requested_effective,
				   int privileged)
{
	ir0_cred_id_triplet_t old;

	if (!ids)
		return -EINVAL;

	old = *ids;
	if (requested_real != UINT32_MAX &&
	    requested_real != old.real &&
	    requested_real != old.effective &&
	    !privileged)
		return -EPERM;
	if (requested_effective != UINT32_MAX &&
	    requested_effective != old.real &&
	    requested_effective != old.effective &&
	    requested_effective != old.saved &&
	    !privileged)
		return -EPERM;

	if (requested_real != UINT32_MAX)
		ids->real = requested_real;
	if (requested_effective != UINT32_MAX)
		ids->effective = requested_effective;
	if (requested_real != UINT32_MAX ||
	    (requested_effective != UINT32_MAX &&
	     requested_effective != old.real))
		ids->saved = ids->effective;

	return 0;
}

static inline int ir0_cred_id_known(const ir0_cred_id_triplet_t *ids,
				    uint32_t want)
{
	if (!ids)
		return 0;
	return want == ids->real || want == ids->effective ||
	       want == ids->saved;
}

/*
 * Linux kernel/sys.c __sys_setresuid()/__sys_setresgid(): each requested
 * slot is independent. Unlike setreuid, saved is not rewritten implicitly.
 */
static inline int ir0_cred_setresid(ir0_cred_id_triplet_t *ids,
				    uint32_t requested_real,
				    uint32_t requested_effective,
				    uint32_t requested_saved,
				    int privileged)
{
	ir0_cred_id_triplet_t old;

	if (!ids)
		return -EINVAL;

	old = *ids;
	if (requested_real != UINT32_MAX && !privileged &&
	    !ir0_cred_id_known(&old, requested_real))
		return -EPERM;
	if (requested_effective != UINT32_MAX && !privileged &&
	    !ir0_cred_id_known(&old, requested_effective))
		return -EPERM;
	if (requested_saved != UINT32_MAX && !privileged &&
	    !ir0_cred_id_known(&old, requested_saved))
		return -EPERM;

	if (requested_real != UINT32_MAX)
		ids->real = requested_real;
	if (requested_effective != UINT32_MAX)
		ids->effective = requested_effective;
	if (requested_saved != UINT32_MAX)
		ids->saved = requested_saved;
	return 0;
}
