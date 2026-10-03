/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness.h"
#include <ir0/utsname.h>
#include <stddef.h>

void test_utsname_linux_abi(void)
{
	struct utsname value;

	TEST_BEGIN("utsname Linux ABI");
	ASSERT(sizeof(value) == 390);
	ASSERT(offsetof(struct utsname, machine) == 260);
	ASSERT(offsetof(struct utsname, domainname) == 325);
	ir0_utsname_init(&value, "IR0", "node", "release", "version", "aarch64");
	ASSERT(value.sysname[0] == 'I' && value.sysname[2] == '0');
	ASSERT(value.machine[0] == 'a');
	ASSERT(value.domainname[0] == '\0');
	ASSERT(value.sysname[64] == '\0' && value.machine[64] == '\0');
	TEST_END();
}
