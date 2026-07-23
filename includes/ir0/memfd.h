/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: memfd.h
 * Description: memfd_create + MAP_SHARED fd mmap (POSIX shm / X11 fd path).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/types.h>
#include <stddef.h>
#include <stdint.h>

#define IR0_MFD_CLOEXEC 0x0001u

struct ir0_memfd;

int64_t sys_memfd_create(const char *uname, unsigned int flags);
struct ir0_memfd *ir0_memfd_alloc(void);
int ir0_memfd_install_fd(struct ir0_memfd *m, int open_flags, int cloexec);
int ir0_memfd_is(const void *ptr);
int ir0_memfd_ftruncate(struct ir0_memfd *m, size_t length);
int ir0_memfd_mmap(struct ir0_memfd *m, uint64_t *pml4, uintptr_t va,
		   size_t length, off_t offset, uint64_t page_flags);
void ir0_memfd_acquire(struct ir0_memfd *m);
void ir0_memfd_release(struct ir0_memfd *m);
size_t ir0_memfd_size(const struct ir0_memfd *m);
