/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: syscall_identity.h
 * Description: Portable process-identity syscalls (Linux current / getpid(2)).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

/*
 * Linux getpid(2) / getppid(2) / getuid(2) read current. IR0 serves the
 * same from current_process when an image binds these ops. ISA decode
 * stays in the arch; this facade is the generic-core owner.
 * man7.org/linux/man-pages/man2/getpid.2.html
 */
struct ir0_syscall_identity_ops
{
	int64_t (*getpid)(void);
	int64_t (*gettid)(void);
	int64_t (*getppid)(void);
	int64_t (*getuid)(void);
	int64_t (*geteuid)(void);
	int64_t (*getgid)(void);
	int64_t (*getegid)(void);
	void (*sync_current)(int64_t pid);
	void (*publish_creds)(uint32_t uid, uint32_t euid,
			      uint32_t gid, uint32_t egid);
};

const struct ir0_syscall_identity_ops *ir0_syscall_identity_product(void);
