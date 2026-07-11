/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: scenario.h
 * Description: KTM integration scenarios (test orchestration).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

#define KTM_OK 0

typedef struct ktm_context
{
	unsigned asserts_pass;
	unsigned asserts_fail;
	unsigned require_abort;
	const char *current_name;
} ktm_context_t;

typedef struct ktm_scenario
{
	const char *name;
	uint32_t flags;
	int (*setup)(ktm_context_t *ctx);
	int (*run)(ktm_context_t *ctx);
	void (*teardown)(ktm_context_t *ctx);
} ktm_scenario_t;

int ktm_scenario_register(const ktm_scenario_t *sc);
int ktm_scenario_run(const char *name);
void ktm_scenarios_run_boot(void);
void ktm_scenarios_register_builtins(void);

unsigned ktm_suite_pass_count(void);
unsigned ktm_suite_fail_count(void);
void ktm_suite_reset(void);
