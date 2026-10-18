/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: early_unix.h
 * Description: Staged AF_UNIX / socketpair object tables
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#include <ir0/early_anonfd.h>
#include <ir0/early_rootfs.h>
#include <ir0/ramtab.h>

#define EARLY_SOCKPAIR_MAX 4
#define EARLY_UNIX_MAX 4
#define EARLY_UNIX_ID_BASE 32

struct early_sockpair
{
	int used;
	int refs[2];
	int dgram;
	int shut[2];
	int reuseaddr;
	int so_error;
	int rights_n[2];
	int rights[2][IR0_EARLY_RIGHTS_MAX];
	int passcred[2];
	int cred_valid[2];
	int cred_explicit[2];
	int32_t cred_pid[2];
	int32_t cred_uid[2];
	int32_t cred_gid[2];
	struct
	{
		unsigned rpos;
		unsigned len;
		uint8_t buf[EARLY_PIPE_BUF];
	} dir[2];
};

struct early_unix
{
	int used;
	int refs;
	int stream;
	int bound_fs;
	int listening;
	int pending_pair;
	int pending_end;
	int peer_fs;
	int reuseaddr;
	int so_error;
	int shut_rd;
	int shut_wr;
	int rights_n;
	int rights[IR0_EARLY_RIGHTS_MAX];
	int abstract;
	unsigned abs_len;
	uint8_t abs_name[IR0_EARLY_SUN_PATH];
	int peer_uid;
	int passcred;
	int cred_valid;
	int cred_explicit;
	int32_t cred_pid;
	int32_t cred_uid;
	int32_t cred_gid;
	char bound_path[RAMTAB_NAME];
	unsigned rpos;
	unsigned len;
	uint8_t buf[EARLY_PIPE_BUF];
};

extern struct early_sockpair g_early_sock[EARLY_SOCKPAIR_MAX];
extern struct early_unix g_early_unix[EARLY_UNIX_MAX];

void early_unix_reset(void);
unsigned early_unix_consume(struct early_unix *u, uint8_t *tmp, unsigned n);
unsigned early_unix_peek(struct early_unix *u, uint8_t *tmp, unsigned n);
unsigned early_unix_deposit(struct early_unix *u, const uint8_t *tmp,
			    unsigned n);
unsigned early_sock_consume(struct early_sockpair *s, int dir, uint8_t *tmp,
			    unsigned n);
unsigned early_sock_peek(struct early_sockpair *s, int dir, uint8_t *tmp,
			 unsigned n);
unsigned early_sock_deposit(struct early_sockpair *s, int dir,
			    const uint8_t *tmp, unsigned n);
