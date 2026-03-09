/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Tests de string (strlen, strcmp) sin proceso.
 */

#include "test/ktest_harness.h"
#include <string.h>

void ktest_string(void)
{
	KTEST_BEGIN("string");

	KASSERT_EQ(strlen(""), (size_t)0);
	KASSERT_EQ(strlen("a"), (size_t)1);
	KASSERT_EQ(strlen("abc"), (size_t)3);

	KASSERT_EQ(strcmp("", ""), 0);
	KASSERT_EQ(strcmp("a", "a"), 0);
	KASSERT(strcmp("a", "b") < 0);
	KASSERT(strcmp("b", "a") > 0);
	KASSERT(strcmp("ab", "abc") < 0);
	KASSERT(strcmp("abc", "ab") > 0);

	KTEST_END();
}
