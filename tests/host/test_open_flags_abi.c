/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * ARCH-5 host contract: Linux open(2) ABI → IR0 internal flags translation.
 */

#include "test_harness.h"
#include <ir0/open_flags.h>

void test_open_flags_linux_to_ir0(void)
{
	int ir0;

	TEST_BEGIN("open_flags_linux_to_ir0");

	ir0 = linux_open_flags_to_ir0(0);
	ASSERT_EQ(IR0_O_RDONLY, ir0);

	ir0 = linux_open_flags_to_ir0(1);
	ASSERT_EQ(IR0_O_WRONLY, ir0);

	ir0 = linux_open_flags_to_ir0(2);
	ASSERT_EQ(IR0_O_RDWR, ir0);

	ir0 = linux_open_flags_to_ir0((int)(LINUX_O_CREAT | LINUX_O_EXCL | LINUX_O_TRUNC));
	ASSERT(ir0 & IR0_O_CREAT);
	ASSERT(ir0 & IR0_O_EXCL);
	ASSERT(ir0 & IR0_O_TRUNC);

	ir0 = linux_open_flags_to_ir0((int)(LINUX_O_APPEND | LINUX_O_NONBLOCK |
					    LINUX_O_CLOEXEC | LINUX_O_DIRECTORY));
	ASSERT(ir0 & IR0_O_APPEND);
	ASSERT(ir0 & IR0_O_NONBLOCK);
	ASSERT(ir0 & IR0_O_CLOEXEC);
	ASSERT(ir0 & IR0_O_DIRECTORY);

	TEST_END();
}

void test_open_flags_vfs_guard(void)
{
	int ir0;

	TEST_BEGIN("open_flags_vfs_guard");

	ASSERT(ir0_open_flags_ok_for_vfs(0));
	ASSERT(ir0_open_flags_ok_for_vfs(IR0_O_RDONLY | IR0_O_CREAT));

	ir0 = linux_open_flags_to_ir0((int)LINUX_O_CREAT);
	ASSERT(ir0_open_flags_ok_for_vfs(ir0));

	ASSERT(!ir0_open_flags_ok_for_vfs((int)LINUX_O_CREAT));
	ASSERT(!ir0_open_flags_ok_for_vfs((int)LINUX_O_NOFOLLOW));
	ASSERT(!ir0_open_flags_ok_for_vfs((int)(LINUX_O_DIRECTORY | IR0_O_RDONLY)));

	TEST_END();
}
