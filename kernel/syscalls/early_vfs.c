/* SPDX-License-Identifier: GPL-3.0-only */
/** Architecture-neutral VFS routing for staged userspace. */

#include <stdint.h>
#include <ir0/early_rootfs.h>
#include <ir0/early_syscall.h>
#include <ir0/early_time.h>
#include <ir0/early_vfs.h>

#define ENOSYS 38

static int64_t early_vfs_syscall(void *opaque, enum ir0_syscall_id id,
				 uint64_t a0, uint64_t a1, uint64_t a2,
				 uint64_t a3, uint64_t a4, uint64_t a5)
{
	struct early_syscall_context *context = opaque;
	const struct early_vfs_services *ops = context && context->services ?
		context->services->vfs : 0;
	const struct early_time_services *time_ops = context && context->services ?
		context->services->time : 0;

	(void)a5;
	if (time_ops && time_ops->counter_read && time_ops->counter_frequency)
		early_rootfs_note_clock(time_ops->counter_read(),
					time_ops->counter_frequency(),
					early_time_realtime_offset());
	if (id == IR0_SYSCALL_GETCWD)
		return early_rootfs_getcwd(a0, a1);
	if (id == IR0_SYSCALL_CHDIR)
		return early_rootfs_chdir(a0);
	if (id == IR0_SYSCALL_GETDENTS64)
		return early_rootfs_getdents64((int)a0, a1, a2);
	if (id == IR0_SYSCALL_LSEEK)
		return early_rootfs_lseek((int)a0, (int64_t)a1, (int)a2);
	if (id == IR0_SYSCALL_FCHDIR)
		return early_rootfs_fchdir((int)a0);
	if (id == IR0_SYSCALL_SYNC)
		return 0;
	if (id == IR0_SYSCALL_FSYNC || id == IR0_SYSCALL_FDATASYNC)
		return early_rootfs_fsync((int)a0);
	if (id == IR0_SYSCALL_STATFS)
		return early_rootfs_statfs(a0, a1);
	if (id == IR0_SYSCALL_FSTATFS)
		return early_rootfs_fstatfs((int)a0, a1);
	if (id == IR0_SYSCALL_FCHMOD)
		return early_rootfs_fchmod((int)a0, a1);
	if (id == IR0_SYSCALL_FCHMODAT)
		return early_rootfs_fchmodat((int)a0, a1, a2);
	if (id == IR0_SYSCALL_FCHOWN)
		return early_rootfs_fchown((int)a0, a1, a2);
	if (id == IR0_SYSCALL_FCHOWNAT)
		return early_rootfs_fchownat((int)a0, a1, a2, a3);
	if (id == IR0_SYSCALL_CHROOT)
		return early_rootfs_chroot(a0);
	if (id == IR0_SYSCALL_TRUNCATE)
		return early_rootfs_truncate(a0, a1);
	if (id == IR0_SYSCALL_FTRUNCATE)
		return early_rootfs_ftruncate((int)a0, a1);
	if (id == IR0_SYSCALL_FLOCK)
		return early_rootfs_flock((int)a0, (int)a1);
	if (id == IR0_SYSCALL_UTIMENSAT)
		return early_rootfs_utimensat((int)a0, a1);
	if (id == IR0_SYSCALL_MKDIRAT)
		return early_rootfs_mkdirat((int)a0, a1);
	if (id == IR0_SYSCALL_MKNODAT)
		return early_rootfs_mknodat((int)a0, a1, a2);
	if (id == IR0_SYSCALL_UNLINKAT)
		return early_rootfs_unlinkat((int)a0, a1, (int)a2);
	if (id == IR0_SYSCALL_RENAMEAT)
		return early_rootfs_renameat((int)a0, a1, (int)a2, a3);
	if (id == IR0_SYSCALL_RENAMEAT2)
		return early_rootfs_renameat2((int)a0, a1, (int)a2, a3,
					      (unsigned)a4);
	if (id == IR0_SYSCALL_SYMLINKAT)
		return early_rootfs_symlinkat(a0, (int)a1, a2);
	if (id == IR0_SYSCALL_LINK)
		return early_rootfs_linkat(IR0_EARLY_ROOTFS_AT_FDCWD, a0,
					   IR0_EARLY_ROOTFS_AT_FDCWD, a1, 0);
	if (id == IR0_SYSCALL_LINKAT)
		return early_rootfs_linkat((int)a0, a1, (int)a2, a3, (int)a4);
	if (id == IR0_SYSCALL_FALLOCATE)
		return early_rootfs_fallocate((int)a0, (int)a1, (int64_t)a2,
					      (int64_t)a3);
	if (id == IR0_SYSCALL_STATX)
	{
		int64_t ret = early_rootfs_statx((int)a0, a1, (int)a2,
						 (uint32_t)a3, a4);

		if (ret == 0 && time_ops && time_ops->note)
			time_ops->note(EARLY_SYSCALL_STATX_OK);
		return ret;
	}
	if (!ops)
		return -ENOSYS;
	switch (id) {
	case IR0_SYSCALL_READ:
		return ops->vfs_read ? ops->vfs_read((int)a0, a1, a2) : -ENOSYS;
	case IR0_SYSCALL_CLOSE:
		return ops->vfs_close ? ops->vfs_close((int)a0) : -ENOSYS;
	case IR0_SYSCALL_OPENAT:
		return ops->vfs_openat ? ops->vfs_openat((int)a0, a1, (int)a2) : -ENOSYS;
	case IR0_SYSCALL_FACCESSAT:
		return ops->vfs_faccessat ? ops->vfs_faccessat((int)a0, a1, (int)a2) : -ENOSYS;
	case IR0_SYSCALL_NEWFSTATAT:
		return ops->vfs_newfstatat ? ops->vfs_newfstatat((int)a0, a1, a2, (int)a3) : -ENOSYS;
	case IR0_SYSCALL_FSTAT:
		return ops->vfs_fstat ? ops->vfs_fstat((int)a0, a1) : -ENOSYS;
	case IR0_SYSCALL_READLINKAT:
		return ops->vfs_readlinkat ? ops->vfs_readlinkat((int)a0, a1, a2, a3) : -ENOSYS;
	default:
		return -ENOSYS;
	}
}

static const enum ir0_syscall_id g_vfs_syscalls[] = {
	IR0_SYSCALL_READ, IR0_SYSCALL_CLOSE, IR0_SYSCALL_OPENAT,
	IR0_SYSCALL_FACCESSAT, IR0_SYSCALL_NEWFSTATAT, IR0_SYSCALL_FSTAT,
	IR0_SYSCALL_READLINKAT, IR0_SYSCALL_GETCWD, IR0_SYSCALL_CHDIR,
	IR0_SYSCALL_GETDENTS64, IR0_SYSCALL_LSEEK, IR0_SYSCALL_FCHDIR,
	IR0_SYSCALL_SYNC, IR0_SYSCALL_FSYNC, IR0_SYSCALL_FDATASYNC,
	IR0_SYSCALL_STATFS, IR0_SYSCALL_FSTATFS, IR0_SYSCALL_FCHMOD,
	IR0_SYSCALL_FCHMODAT, IR0_SYSCALL_FCHOWN, IR0_SYSCALL_FCHOWNAT,
	IR0_SYSCALL_CHROOT, IR0_SYSCALL_TRUNCATE, IR0_SYSCALL_FTRUNCATE,
	IR0_SYSCALL_FLOCK, IR0_SYSCALL_UTIMENSAT, IR0_SYSCALL_MKDIRAT,
	IR0_SYSCALL_MKNODAT, IR0_SYSCALL_UNLINKAT, IR0_SYSCALL_RENAMEAT,
	IR0_SYSCALL_SYMLINKAT,
	IR0_SYSCALL_LINK, IR0_SYSCALL_LINKAT,
	IR0_SYSCALL_FALLOCATE, IR0_SYSCALL_STATX, IR0_SYSCALL_RENAMEAT2,
};

const struct syscall_context_provider early_vfs_provider = {
	.ids = g_vfs_syscalls,
	.count = sizeof(g_vfs_syscalls) / sizeof(g_vfs_syscalls[0]),
	.handler = early_vfs_syscall,
};
