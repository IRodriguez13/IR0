/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 *
 * Portable memory-management façade for callers that must not embed
 * `<mm/...>` include paths directly (e.g. filesystem pseudo-nodes).
 *
 * Delegates to PMM, heap allocator, and paging constants as needed.
 */

#pragma once

#include <mm/pmm.h>
#include <mm/allocator.h>
#include <mm/paging.h>
