/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: mm_diag.h
 * Description: MM diagnostics facade (mm/diag.c, implemented over KTM).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

/*
 * Nonzero when the named KTM fault-injection point fires; MM allocation
 * paths use it to exercise their failure branches.
 */
int mm_diag_fault_hit(const char *name);
