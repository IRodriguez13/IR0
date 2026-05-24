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

void exec_read_trace_minix_file_begin(const char *path, uint16_t inode_num,
				      uint16_t mode, uint32_t size);

void exec_read_trace_minix_zones(const uint16_t zones[9]);

void exec_read_trace_minix_block(const char *kind, int block_index,
				 uint32_t zone_num, uint32_t disk_block,
				 uint32_t lba, size_t file_offset,
				 size_t req, size_t copied);

void exec_read_trace_minix_eio(const char *classify, const char *kind,
			       int block_index, uint32_t zone_num,
			       uint32_t disk_block, uint32_t lba,
			       size_t file_offset, void *buf);

void exec_read_trace_device_read(uint32_t lba, uint8_t n_sectors, int ok,
				 void *buf);

#endif /* _IR0_EXEC_READ_TRACE_H */
