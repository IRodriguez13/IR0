/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: dyn_hello_lib.c
 * Description: Shared object for the dyn_hello selftest: code, relocated
 *              data and a constructor ld.so must run.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

int dyn_hello_counter = 41;

static const char *greeting = "hello from libdynhello";

__attribute__((constructor)) static void dyn_hello_init(void)
{
	dyn_hello_counter++;
}

const char *dyn_hello_greeting(void)
{
	return greeting;
}
