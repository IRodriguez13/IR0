/* SPDX-License-Identifier: GPL-3.0-only */
/** Architecture-neutral VFS routing for staged userspace. */

#include <stdint.h>
#include <ir0/early_rootfs.h>
#include <ir0/early_syscall.h>
#include <ir0/early_vfs.h>

#define ENOSYS 38

static int64_t early_vfs_syscall(void *opaque, enum ir0_syscall_id id,
				 uint64_t a0, uint64_t a1, uint64_t a2,
				 uint64_t a3, uint64_t a4, uint64_t a5)
{
	struct early_syscall_context *context = opaque;
	const struct early_vfs_services *ops = context && context->services ?
		context->services->vfs : 0;

	(void)a4; (void)a5;
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
	IR0_SYSCALL_STATFS, IR0_SYSCALL_FSTATFS,
};

const struct syscall_context_provider early_vfs_provider = {
	.ids = g_vfs_syscalls,
	.count = sizeof(g_vfs_syscalls) / sizeof(g_vfs_syscalls[0]),
	.handler = early_vfs_syscall,
};
