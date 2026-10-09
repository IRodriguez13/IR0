/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: fd_resource.h
 * Description: Open-resource lifetime contract carried by fd entries.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/fd_types.h>

typedef struct fd_resource_ops
{
	const char *name;
	int (*acquire)(fd_entry_t *entry);
	void (*release)(fd_entry_t *entry);
} fd_resource_ops_t;

int fd_resource_acquire(fd_entry_t *entry);
int fd_resource_release(fd_entry_t *entry);
void fd_resource_forget(fd_entry_t *entry);

/* Compatibility binder used by current providers while they migrate ownership. */
int fd_resource_bind_legacy(fd_entry_t *entry);
