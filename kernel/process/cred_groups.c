/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: cred_groups.c
 * Description: Process supplementary-group invariants shared by create/syscalls.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process_internal.h"

void process_cred_init_groups(process_t *p)
{
	if (!p)
		return;
	if (p->ngroups == 0)
	{
		p->groups[0] = (gid_t)p->gid;
		p->ngroups = 1;
	}
}
