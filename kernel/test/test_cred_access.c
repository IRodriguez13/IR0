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
#include <ir0/cred.h>
#include <ir0/stat.h>
#include <ir0/ktm/klog.h>
#include <string.h>

void ktest_cred_access_contract(void)
{
	struct ir0_task_cred root;
	struct ir0_task_cred user;
	stat_t st;
	gid_t supp[1];

	KTEST_BEGIN("cred_access_contract");

	memset(&root, 0, sizeof(root));
	root.euid = ROOT_UID;
	root.egid = ROOT_GID;
	memset(&user, 0, sizeof(user));
	user.euid = USER_UID;
	user.egid = USER_GID;

	memset(&st, 0, sizeof(st));
	st.st_mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
	st.st_uid = USER_UID;
	st.st_gid = USER_GID;

	KASSERT_EQ(generic_permission(&root, &st, ACCESS_READ), 0);
	KASSERT_EQ(generic_permission(&user, &st, ACCESS_WRITE), 0);

	st.st_mode = S_IRUSR | S_IWUSR | S_IXUSR;
	st.st_uid = ROOT_UID;
	KASSERT_EQ(generic_permission(&user, &st, ACCESS_WRITE), -EACCES);

	supp[0] = USER_GID;
	user.egid = (gid_t)(USER_GID + 1);
	user.groups = supp;
	user.ngroups = 1;
	st.st_uid = ROOT_UID;
	st.st_gid = USER_GID;
	st.st_mode = S_IRGRP;
	KASSERT_EQ(generic_permission(&user, &st, ACCESS_READ), 0);
	KASSERT_EQ(generic_permission(NULL, &st, ACCESS_READ), -EACCES);
	KASSERT_EQ(generic_permission(&user, NULL, ACCESS_READ), -EACCES);

	klog_smoke("MULTIUSER_PERMS_OK");
	KTEST_END();
}
