/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness_ir0.h"
#include <ir0/fd_resource.h>
#include <ir0/errno.h>
#include <string.h>

static int acquired;
static int released;

static int mock_acquire(fd_entry_t *entry)
{
	ASSERT(entry != NULL);
	acquired++;
	return 0;
}

static void mock_release(fd_entry_t *entry)
{
	ASSERT(entry != NULL);
	released++;
}

void test_fd_resource_contract(void)
{
	static const fd_resource_ops_t ops = {
		.name = "host-mock",
		.acquire = mock_acquire,
		.release = mock_release,
	};
	fd_entry_t entry;

	TEST_BEGIN("fd_resource_provider_contract");
	memset(&entry, 0, sizeof(entry));
	entry.in_use = true;
	entry.vfs_file = &entry;
	ASSERT(fd_resource_acquire(&entry) == -EINVAL);
	ASSERT(fd_resource_release(&entry) == -EINVAL);
	entry.resource_ops = &ops;
	ASSERT(fd_resource_acquire(&entry) == 0);
	ASSERT(acquired == 1);
	ASSERT(fd_resource_release(&entry) == 0);
	ASSERT(released == 1);
	ASSERT(entry.vfs_file == NULL);
	fd_resource_forget(&entry);
	ASSERT(entry.resource_ops == NULL);
	TEST_END();
}
