/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: cred_transition.h
 * Description: Linux saved-ID transition helper for setreuid/setregid
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/errno.h>
#include <stdint.h>

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
