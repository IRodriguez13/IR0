/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: passwd.h
 * Description: IR0 — /etc/passwd and /etc/group subset (passwd(5) / group(5))
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <ir0/types.h>

#define IR0_PASSWD_MAX_ENTRIES 32
#define IR0_PASSWD_NAME_MAX    32

struct ir0_passwd_entry
{
	uid_t uid;
	gid_t gid;
	char name[IR0_PASSWD_NAME_MAX];
};

struct ir0_group_entry
{
	gid_t gid;
	char name[IR0_PASSWD_NAME_MAX];
	gid_t members[IR0_PASSWD_MAX_ENTRIES];
	uint8_t nmembers;
};

void ir0_passwd_reload(void);
int ir0_passwd_lookup_uid(uid_t uid, struct ir0_passwd_entry *out);
int ir0_passwd_lookup_name(const char *name, struct ir0_passwd_entry *out);
int ir0_group_lookup_gid(gid_t gid, struct ir0_group_entry *out);
