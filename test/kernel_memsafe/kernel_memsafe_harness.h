/*
 * Harness mínimo para kernel-memsafe: macros de test.
 */

#ifndef KERNEL_MEMSAFE_HARNESS_H
#define KERNEL_MEMSAFE_HARNESS_H

#include <stdio.h>

extern int tests_run;
extern int tests_failed;

#define KTEST_BEGIN(name) do { \
	tests_run++; \
	fprintf(stderr, "[KERNEL-MEMSAFE] %s ... ", (name)); \
	fflush(stderr); \
} while (0)

#define KTEST_END() do { \
	if (tests_failed) { \
		fprintf(stderr, "FAIL\n"); \
	} else { \
		fprintf(stderr, "PASS\n"); \
	} \
} while (0)

#define KASSERT(c) do { if (!(c)) { tests_failed = 1; fprintf(stderr, "\n  ASSERT failed: %s\n", #c); } } while (0)

#endif
