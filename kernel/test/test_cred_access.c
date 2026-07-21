/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_cred_access.c
 * Description: credential / permission contract (owner/group/other)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test/ktest_harness.h"
#include <ir0/permissions.h>
#include <ir0/stat.h>
#include <ir0/ktm/klog.h>
#include <string.h>

void ktest_cred_access_contract(void)
{
	stat_t st;
	gid_t supp[1];

	KTEST_BEGIN("cred_access_contract");

	memset(&st, 0, sizeof(st));
	st.st_mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
	st.st_uid = USER_UID;
	st.st_gid = USER_GID;

	KASSERT(ir0_access_from_stat_groups(&st, ACCESS_READ, ROOT_UID,
					    ROOT_GID, NULL, 0));
	KASSERT(ir0_access_from_stat_groups(&st, ACCESS_WRITE, USER_UID,
					     USER_GID, NULL, 0));

	st.st_mode = S_IRUSR | S_IWUSR | S_IXUSR;
	st.st_uid = ROOT_UID;
	KASSERT(!ir0_access_from_stat_groups(&st, ACCESS_WRITE, USER_UID,
					     USER_GID, NULL, 0));

	supp[0] = USER_GID;
	st.st_uid = ROOT_UID;
	st.st_gid = USER_GID;
	st.st_mode = S_IRGRP;
	KASSERT(ir0_access_from_stat_groups(&st, ACCESS_READ, USER_UID,
					     (gid_t)(USER_GID + 1), supp, 1));

	klog_smoke("MULTIUSER_PERMS_OK");
	KTEST_END();
}
