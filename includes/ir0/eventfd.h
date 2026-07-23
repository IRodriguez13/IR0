/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: eventfd.h
 * Description: eventfd2 counter fd (pollable event loop primitive).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <ir0/types.h>

#define IR0_EFD_CLOEXEC  0x80000u
#define IR0_EFD_NONBLOCK 0x800u

struct ir0_eventfd;

int64_t sys_eventfd2(unsigned int count, int flags);
int ir0_eventfd_is(const void *ptr);
int64_t ir0_eventfd_read(struct ir0_eventfd *e, void *buf, size_t count, int nonblock);
int64_t ir0_eventfd_write(struct ir0_eventfd *e, const void *buf, size_t count, int nonblock);
int ir0_eventfd_poll_readable(const struct ir0_eventfd *e);
int ir0_eventfd_poll_writable(const struct ir0_eventfd *e);
void ir0_eventfd_acquire(struct ir0_eventfd *e);
void ir0_eventfd_release(struct ir0_eventfd *e);
