// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — EXEC-only busybox read path serial diagnostics.
 */

#include "exec_read_trace.h"
#include <ir0/serial_io.h>

int vfs_exec_audit_is_active(void);

static int exec_read_trace_on(void)
{
	return vfs_exec_audit_is_active();
}

void exec_read_trace_vfs_read_file(const char *path, uint64_t ino, int64_t size,
				     off_t offset, size_t req, int ret)
{
	if (!exec_read_trace_on())
		return;

	serial_print("[EXEC_ONLY][VFS] stage=read_file path=");
	serial_print(path ? path : "(null)");
	serial_print(" inode=");
	serial_print_hex64(ino);
	serial_print(" size=");
	serial_print_hex64((uint64_t)size);
	serial_print(" offset=");
	serial_print_hex64((uint64_t)offset);
	serial_print(" req=");
	serial_print_hex64((uint64_t)req);
	serial_print(" ret=");
	serial_print_hex64((uint64_t)(int64_t)ret);
	serial_print("\n");
}

void exec_read_trace_minix_file_begin(const char *path, uint16_t inode_num,
				      uint16_t mode, uint32_t size)
{
	if (!exec_read_trace_on())
		return;

	serial_print("[EXEC_ONLY][MINIX] stage=read_file_begin path=");
	serial_print(path ? path : "(null)");
	serial_print(" inode=");
	serial_print_hex32((uint32_t)inode_num);
	serial_print(" mode=0x");
	serial_print_hex32((uint32_t)mode);
	serial_print(" size=");
	serial_print_hex32(size);
	serial_print("\n");
}

void exec_read_trace_minix_zones(const uint16_t zones[9])
{
	int i;

	if (!exec_read_trace_on() || !zones)
		return;

	serial_print("[EXEC_ONLY][MINIX] stage=zones");
	for (i = 0; i < 9; i++)
	{
		serial_print(" z");
		serial_print_hex32((uint32_t)i);
		serial_print("=");
		serial_print_hex32((uint32_t)zones[i]);
	}
	serial_print("\n");
}

void exec_read_trace_minix_block(const char *kind, int block_index,
				 uint32_t zone_num, uint32_t disk_block,
				 uint32_t lba, size_t file_offset,
				 size_t req, size_t copied)
{
	if (!exec_read_trace_on())
		return;

	serial_print("[EXEC_ONLY][MINIX] stage=block kind=");
	serial_print(kind ? kind : "?");
	serial_print(" blk_idx=");
	serial_print_hex32((uint32_t)block_index);
	serial_print(" zone=");
	serial_print_hex32(zone_num);
	serial_print(" disk_block=");
	serial_print_hex32(disk_block);
	serial_print(" lba=");
	serial_print_hex32(lba);
	serial_print(" file_off=");
	serial_print_hex64((uint64_t)file_offset);
	serial_print(" req=");
	serial_print_hex64((uint64_t)req);
	serial_print(" copied=");
	serial_print_hex64((uint64_t)copied);
	serial_print(" cache=none shared_buf=0\n");
}

void exec_read_trace_minix_eio(const char *classify, const char *kind,
			       int block_index, uint32_t zone_num,
			       uint32_t disk_block, uint32_t lba,
			       size_t file_offset, void *buf)
{
	if (!exec_read_trace_on())
		return;

	serial_print("[EXEC_ONLY][MINIX] stage=eio errno=-EIO kind=");
	serial_print(kind ? kind : "?");
	serial_print(" blk_idx=");
	serial_print_hex32((uint32_t)block_index);
	serial_print(" zone=");
	serial_print_hex32(zone_num);
	serial_print(" disk_block=");
	serial_print_hex32(disk_block);
	serial_print(" lba=");
	serial_print_hex32(lba);
	serial_print(" file_off=");
	serial_print_hex64((uint64_t)file_offset);
	serial_print(" buf=");
	serial_print_hex64((uint64_t)(uintptr_t)buf);
	serial_print("\n");
	if (classify)
	{
		serial_print("[EXEC_ONLY][CLASSIFY] ");
		serial_print(classify);
		serial_print("\n");
	}
}

void exec_read_trace_device_read(uint32_t lba, uint8_t n_sectors, int ok,
				 void *buf)
{
	if (!exec_read_trace_on())
		return;

	serial_print("[EXEC_ONLY][DEV] stage=read_sectors lba=");
	serial_print_hex32(lba);
	serial_print(" n=");
	serial_print_hex32((uint32_t)n_sectors);
	serial_print(" ret=");
	serial_print(ok ? "ok" : "fail");
	serial_print(" buf=");
	serial_print_hex64((uint64_t)(uintptr_t)buf);
	serial_print(" cache=none\n");
	if (!ok)
	{
		serial_print("[EXEC_ONLY][CLASSIFY] DEVICE_READ_FLAKE\n");
	}
}
