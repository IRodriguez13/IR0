/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: dyn_hello.c
 * Description: Selftest: a dynamically linked musl program. execve loads
 *              PT_INTERP (ld-musl), which maps libc and libdynhello.so
 *              MAP_PRIVATE, relocates them and runs their constructors.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <elf.h>
#include <stdio.h>
#include <string.h>
#include <sys/auxv.h>

#include "ir0/ksft.h"

extern int dyn_hello_counter;
const char *dyn_hello_greeting(void);

int main(void)
{
	const char *g = dyn_hello_greeting();

	ksft_set_plan(3);
	/* Linux binfmt_elf: AT_BASE is the interpreter, AT_ENTRY the program. */
	ksft_test_result(getauxval(AT_BASE) != 0 && getauxval(AT_ENTRY) != 0 &&
			 getauxval(AT_PHDR) != 0, "auxv_names_interpreter");
	ksft_test_result(g && strcmp(g, "hello from libdynhello") == 0 &&
			 dyn_hello_counter == 42, "shared_object_linked");
	ksft_test_result(printf("# %s, counter=%d\n", g, dyn_hello_counter) > 0 &&
			 fflush(stdout) == 0, "dynamic_hello");
	return ksft_exit();
}
