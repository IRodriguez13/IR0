/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: test_fcntl_abi.c
 * Description: Linux fcntl command numbers + F_DUPFD_CLOEXEC contract.
 */

#include "test_harness.h"
#include <ir0/fcntl.h>

void test_fcntl_linux_cmd_numbers(void)
{
	TEST_BEGIN("fcntl_linux_cmd_numbers");

	ASSERT_EQ(0, F_DUPFD);
	ASSERT_EQ(1, F_GETFD);
	ASSERT_EQ(2, F_SETFD);
	ASSERT_EQ(3, F_GETFL);
	ASSERT_EQ(4, F_SETFL);
	ASSERT_EQ(5, F_GETLK);
	ASSERT_EQ(6, F_SETLK);
	ASSERT_EQ(7, F_SETLKW);
	ASSERT_EQ(8, F_SETOWN);
	ASSERT_EQ(9, F_GETOWN);
	ASSERT_EQ(1030, F_DUPFD_CLOEXEC);
	ASSERT_EQ(1, FD_CLOEXEC);

	TEST_END();
}
