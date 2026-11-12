/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: kernel.h
 * Description: IR0 core kernel globals and bring-up entry points.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

/*
 * Main kernel entry point. firmware_handle is architecture-defined (physical):
 * x86-64: Multiboot info address (legacy ebx); may be 0.
 */
void kmain(uintptr_t firmware_handle);
