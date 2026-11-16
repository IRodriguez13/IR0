/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — EXEC-only busybox read path serial diagnostics.
 * Active while vfs_exec_audit is enabled (ELF exec of a file).
 */

#ifndef _IR0_EXEC_READ_TRACE_H
#define _IR0_EXEC_READ_TRACE_H

#include <stddef.h>
#include <stdint.h>
#include <ir0/types.h>

void exec_read_trace_vfs_read_file(const char *path, uint64_t ino, int64_t size,
				     off_t offset, size_t req, int ret);

void exec_read_trace_device_read(uint32_t lba, uint8_t n_sectors, int ok,
				 void *buf);

#endif /* _IR0_EXEC_READ_TRACE_H */
