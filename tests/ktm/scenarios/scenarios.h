/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: scenarios.h
 * Description: Registration entry points of the built-in KTM scenarios,
 *              called by ktm_scenarios_register_builtins().
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <config.h>

void ktm_scenario_register_pipe_lifecycle(void);
void ktm_scenario_register_mm_cow_fork(void);
void ktm_scenario_register_mm_vma(void);
void ktm_scenario_register_mm_page_tables(void);
void ktm_scenario_register_mm_steady_state(void);
void ktm_scenario_register_vfs_devfs(void);
void ktm_scenario_register_shell_redir(void);
void ktm_scenario_register_mm_oom_class(void);
void ktm_scenario_register_process_wait_drain(void);
void ktm_scenario_register_graphics_fb(void);
void ktm_scenario_register_input_events0(void);
void ktm_scenario_register_vfs_open_flags(void);
void ktm_scenario_register_process_reclaim_exit(void);
void ktm_scenario_register_process_exec(void);
void ktm_scenario_register_process_fork_rollback(void);
void ktm_scenario_register_ktest_bridge(void);
#if defined(CONFIG_KTM_BLOCK_FAKE) && CONFIG_KTM_BLOCK_FAKE
void ktm_scenario_register_block_read_eio(void);
#endif
