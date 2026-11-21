/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: signal_contract.h
 * Description: Portable signal-delivery stack layout contract (poly-ISA).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#include <asm/signal_contract.h>

/*
 * Bytes reserved just below the sigframe for the handler's return address
 * (the restorer). Zero when the ISA returns through a link register.
 */
#define SIGFRAME_RETURN_SLOT IR0_ARCH_SIGFRAME_RETURN_SLOT

/* Bytes below the interrupted SP the frame must not touch (ABI red zone). */
#define SIGFRAME_RED_ZONE IR0_ARCH_SIGFRAME_RED_ZONE

/* Smallest alternate signal stack sigaltstack(2) accepts (Linux MINSIGSTKSZ). */
#define MINSIGSTKSZ IR0_ARCH_MINSIGSTKSZ

