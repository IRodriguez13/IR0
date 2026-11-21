/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: port.h
 * Description: Comment contract of required ISA port symbols for a new
 *              architecture. Not included by the build.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

/*
 * A new ISA implements each symbol below as a normal strong definition.
 * The line after the name is the header that declares it. This file is
 * not wired into the build: no includes, no weak aliases, no bodies.
 *
 * ir0_oops_dump_arch_extra
 *     includes/ir0/oops_port.h
 * ir0_oops_show_fault_regs
 *     includes/ir0/oops_port.h
 * ir0_oops_user_fault_extra
 *     includes/ir0/oops_port.h
 *
 * ir0_boot_arch_firmware_early
 *     includes/ir0/boot_arch.h
 * ir0_boot_arch_userspace_handoff_smoke
 *     includes/ir0/boot_arch.h
 *
 * ir0_boot_port_params
 *     includes/ir0/boot_port.h
 * ir0_boot_port_snapshot_fill
 *     includes/ir0/boot_port.h
 * ir0_boot_port_serial_before_logging
 *     includes/ir0/boot_port.h
 * ir0_boot_port_serial_after_driver_registry
 *     includes/ir0/boot_port.h
 * ir0_boot_port_maybe_seed_init_from_embedded
 *     includes/ir0/boot_port.h
 *
 * ir0_boot_firmware_sysfs_fill_from_arch
 *     includes/ir0/boot_firmware_sysfs.h
 *
 * arm64_mmu_map_device_block
 *     arch/arm64/sources/mmu.h
 */
