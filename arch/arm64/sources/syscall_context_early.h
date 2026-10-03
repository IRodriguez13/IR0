/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

struct arm64_early_syscall_context
{
	int *leave_el0;
	int (*smoke_ok)(void);
};
