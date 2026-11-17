// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — serial diagnostics of the file reads execve(2) makes.
 */

#include <ir0/exec_read_trace.h>
#include <ir0/ktm/klog.h>
#include <fs/vfs.h>

static int exec_read_trace_on(void)
{
	return vfs_exec_audit_is_active();
}

void exec_read_trace_vfs_read_file(const char *path, uint64_t ino, int64_t size,
				     off_t offset, size_t req, int ret)
{
	if (!exec_read_trace_on())
		return;

	klog_info_fmt("EXEC",
		      "[EXEC_ONLY][VFS] stage=read_file path=%s inode=0x%llx size=0x%llx offset=0x%llx req=0x%llx ret=0x%llx",
		      path ? path : "(null)", (unsigned long long)ino,
		      (unsigned long long)size, (unsigned long long)offset,
		      (unsigned long long)req, (unsigned long long)(int64_t)ret);
}

void exec_read_trace_device_read(uint32_t lba, uint8_t n_sectors, int ok,
				 void *buf)
{
	if (!exec_read_trace_on())
		return;

	klog_info_fmt("EXEC",
		      "[EXEC_ONLY][DEV] stage=read_sectors lba=0x%x n=0x%x ret=%s buf=0x%llx cache=none",
		      (unsigned)lba, (unsigned)n_sectors, ok ? "ok" : "fail",
		      (unsigned long long)(uintptr_t)buf);
	if (!ok)
		klog_info("EXEC", "CLASSIFY DEVICE_READ_FLAKE");
}
