/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_fix_contracts.c
 * Description: ktest cases that fail if a recent kernel fix is reverted.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test/ktest_harness.h"
#include "process.h"
#include "syscalls.h"
#include <kernel/syscalls/fs_path_syscalls.h>
#include <kernel/syscalls/fs_syscalls.h>
#include <kernel/syscalls/socket_syscalls.h>
#include <ir0/arch_mm.h>
#include <ir0/errno.h>
#include <ir0/fcntl.h>
#include <ir0/files_struct.h>
#include <ir0/futex.h>
#include <ir0/ipc.h>
#include <ir0/kmem.h>
#include <ir0/mman.h>
#include <ir0/paging.h>
#include <ir0/pmm.h>
#include <ir0/signals.h>
#include <ir0/stat.h>
#include <ir0/stat_user.h>
#include <ir0/sysv_shm.h>
#include <ir0/ktm/fault.h>
#include <ir0/vfs.h>
#include <string.h>

static int ktest_mount_rw(const char *path)
{
	int64_t rc;

	rc = sys_mkdir(path, 0755);
	if (rc != 0 && rc != -EEXIST)
		return (int)rc;
	rc = sys_mount("none", path, "tmpfs", 0, NULL);
	if (rc == -EBUSY)
	{
		(void)sys_umount(path, 0);
		rc = sys_mount("none", path, "tmpfs", 0, NULL);
	}
	return (int)rc;
}

void ktest_readlink_returns_copied_bytes(void)
{
	char full[64];
	char one;
	int64_t full_len;
	int64_t short_len;
	const char *path = "/proc/self/fd/0";

	KTEST_BEGIN("readlink_returns_copied_bytes");
	memset(full, 0, sizeof(full));
	full_len = sys_readlink(path, full, sizeof(full));
	if (full_len <= 1)
	{
		path = "/proc/self/exe";
		memset(full, 0, sizeof(full));
		full_len = sys_readlink(path, full, sizeof(full));
	}
	KASSERT_GT(full_len, 1);
	short_len = sys_readlink(path, &one, 1);
	KASSERT_EQ(short_len, (int64_t)1);
	KTEST_END();
}

void ktest_fstat_redirected_stdio_is_file(void)
{
	int64_t file_fd;
	int64_t saved;
	struct ir0_user_stat st;
	int mount_rc;

	KTEST_BEGIN("fstat_redirected_stdio_is_file");
	mount_rc = ktest_mount_rw("/mntfix-fstat");
	KASSERT_EQ(mount_rc, 0);
	if (mount_rc != 0)
	{
		KTEST_END();
		return;
	}

	file_fd = sys_open("/mntfix-fstat/reg",
			   KTEST_O_CREAT | KTEST_O_RDWR | KTEST_O_TRUNC, 0644);
	KASSERT_GT(file_fd, 2);
	saved = sys_dup(0);
	if (file_fd > 2)
	{
		KASSERT_EQ(sys_dup2((int)file_fd, 0), (int64_t)0);
		memset(&st, 0, sizeof(st));
		KASSERT_EQ(sys_fstat(0, (stat_t *)&st), (int64_t)0);
		KASSERT(S_ISREG(st.st_mode));
		KASSERT(!S_ISCHR(st.st_mode));
	}
	if (saved >= 0)
		(void)sys_dup2((int)saved, 0);
	else
		(void)sys_close(0);
	if (saved >= 0)
		(void)sys_close((int)saved);
	if (file_fd > 2)
		(void)sys_close((int)file_fd);
	(void)vfs_remount("/mntfix-fstat", IR0_MS_REMOUNT);
	(void)sys_umount("/mntfix-fstat", 0);
	KTEST_END();
}

void ktest_lseek_redirected_stdio_uses_file(void)
{
	int64_t file_fd;
	int64_t saved;
	int mount_rc;
	const char *payload = "abcd";

	KTEST_BEGIN("lseek_redirected_stdio_uses_file");
	mount_rc = ktest_mount_rw("/mntfix-lseek");
	KASSERT_EQ(mount_rc, 0);
	if (mount_rc != 0)
	{
		KTEST_END();
		return;
	}

	file_fd = sys_open("/mntfix-lseek/reg",
			   KTEST_O_CREAT | KTEST_O_RDWR | KTEST_O_TRUNC, 0644);
	KASSERT_GT(file_fd, 2);
	if (file_fd > 2)
	{
		KASSERT_EQ(sys_write((int)file_fd, payload, 4), (int64_t)4);
		saved = sys_dup(1);
		KASSERT_EQ(sys_dup2((int)file_fd, 1), (int64_t)1);
		KASSERT_EQ(sys_lseek(1, 0, SEEK_END), (int64_t)4);
		if (saved >= 0)
		{
			(void)sys_dup2((int)saved, 1);
			(void)sys_close((int)saved);
		}
		(void)sys_close((int)file_fd);
	}
	(void)vfs_remount("/mntfix-lseek", IR0_MS_REMOUNT);
	(void)sys_umount("/mntfix-lseek", 0);
	KTEST_END();
}

void ktest_read_efault_keeps_offset(void)
{
	const char *path = "/mntfix-readoff/reg";
	int64_t file_fd = -1;
	int mount_rc;
	process_mode_t saved;
	char got[2];
	uintptr_t bad;
	int64_t rc;

	KTEST_BEGIN("read_efault_keeps_offset");
	KASSERT(current_process != NULL);
	if (!current_process)
	{
		KTEST_END();
		return;
	}

	mount_rc = ktest_mount_rw("/mntfix-readoff");
	KASSERT_EQ(mount_rc, 0);
	if (mount_rc != 0)
	{
		KTEST_END();
		return;
	}

	file_fd = sys_open(path, KTEST_O_CREAT | KTEST_O_RDWR | KTEST_O_TRUNC,
			   0644);
	KASSERT_GT(file_fd, 2);
	if (file_fd <= 2)
		goto out;

	KASSERT_EQ(sys_write((int)file_fd, "AB", 2), (int64_t)2);
	KASSERT_EQ(sys_lseek((int)file_fd, 0, SEEK_SET), (int64_t)0);

	/*
	 * Validate a user VA that passes the range check but has no PTE, so
	 * copy_to_user() fails after the VFS read has already advanced f_pos.
	 */
	bad = mm_user_va_start();
	if (is_page_mapped_in_directory(process_pgd(current_process), bad,
					NULL) == 1)
		bad = mm_user_va_start() + 0x00B00000UL;

	saved = current_process->mode;
	current_process->mode = USER_MODE;
	rc = sys_read((int)file_fd, (void *)bad, 2);
	current_process->mode = saved;
	KASSERT_EQ(rc, (int64_t)-EFAULT);

	/* A failed read must not consume the file; the same bytes stay there. */
	memset(got, 0, sizeof(got));
	rc = sys_read((int)file_fd, got, 2);
	KASSERT_EQ(rc, (int64_t)2);
	KASSERT(got[0] == 'A' && got[1] == 'B');

out:
	if (file_fd > 2)
		(void)sys_close((int)file_fd);
	(void)vfs_remount("/mntfix-readoff", IR0_MS_REMOUNT);
	(void)sys_umount("/mntfix-readoff", 0);
	KTEST_END();
}

void ktest_socket_read_efault_keeps_bytes(void)
{
	int sv[2] = { -1, -1 };
	process_mode_t saved;
	char got[2];
	uintptr_t bad;
	int64_t rc;

	KTEST_BEGIN("socket_read_efault_keeps_bytes");
	KASSERT(current_process != NULL);
	if (!current_process)
	{
		KTEST_END();
		return;
	}

	rc = sys_socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sv);
	KASSERT_EQ(rc, (int64_t)0);
	if (rc != 0)
	{
		KTEST_END();
		return;
	}

	KASSERT_EQ(sys_write(sv[0], "AB", 2), (int64_t)2);

	bad = mm_user_va_start();
	if (is_page_mapped_in_directory(process_pgd(current_process), bad,
					NULL) == 1)
		bad = mm_user_va_start() + 0x00B00000UL;

	saved = current_process->mode;
	current_process->mode = USER_MODE;
	rc = sys_read(sv[1], (void *)bad, 2);
	current_process->mode = saved;
	KASSERT_EQ(rc, (int64_t)-EFAULT);

	/* EFAULT must not dequeue the peer's bytes. */
	memset(got, 0, sizeof(got));
	rc = sys_read(sv[1], got, 2);
	KASSERT_EQ(rc, (int64_t)2);
	KASSERT(got[0] == 'A' && got[1] == 'B');

	(void)sys_close(sv[0]);
	(void)sys_close(sv[1]);
	KTEST_END();
}

void ktest_fsync_null_fd_table(void)
{
	files_struct_t *files;
	uint32_t saved_magic;
	struct ir0_user_stat st;

	KTEST_BEGIN("fsync_null_fd_table");
	KASSERT(current_process != NULL);
	if (!current_process)
	{
		KTEST_END();
		return;
	}
	files = current_process->files;
	KASSERT(files != NULL);
	if (!files)
	{
		KTEST_END();
		return;
	}

	saved_magic = files->magic;
	files->magic = 0;
	KASSERT_EQ(sys_fstat(0, (stat_t *)&st), (int64_t)-EBADF);
	KASSERT_EQ(sys_fsync(0), (int64_t)-EBADF);
	KASSERT_EQ(sys_ftruncate(0, 0), (int64_t)-EBADF);
	files->magic = saved_magic;
	KTEST_END();
}

void ktest_rename_keeps_unlink_errno(void)
{
	int64_t old_fd;
	int64_t new_fd;
	int mount_rc;
	int64_t rc;

	KTEST_BEGIN("rename_keeps_unlink_errno");
	mount_rc = ktest_mount_rw("/mntfix-rename");
	KASSERT_EQ(mount_rc, 0);
	if (mount_rc != 0)
	{
		KTEST_END();
		return;
	}

	old_fd = sys_open("/mntfix-rename/old",
			  KTEST_O_CREAT | KTEST_O_RDWR | KTEST_O_TRUNC, 0644);
	new_fd = sys_open("/mntfix-rename/new",
			  KTEST_O_CREAT | KTEST_O_RDWR | KTEST_O_TRUNC, 0644);
	KASSERT_GT(old_fd, 2);
	KASSERT_GT(new_fd, 2);
	if (old_fd > 2)
		(void)sys_close((int)old_fd);
	if (new_fd > 2)
		(void)sys_close((int)new_fd);

	KASSERT_EQ(vfs_remount("/mntfix-rename", IR0_MS_REMOUNT | IR0_MS_RDONLY), 0);
	rc = sys_rename("/mntfix-rename/old", "/mntfix-rename/new");
	KASSERT_EQ(rc, (int64_t)-EROFS);
	(void)vfs_remount("/mntfix-rename", IR0_MS_REMOUNT);
	(void)sys_umount("/mntfix-rename", 0);
	KTEST_END();
}

void ktest_pipe_read_efault_keeps_bytes(void)
{
	int pipefd[2];
	process_mode_t saved;
	char got[2];
	int64_t rc;
	uintptr_t bad = mm_user_va_start();

	KTEST_BEGIN("pipe_read_efault_keeps_bytes");
	KASSERT(current_process != NULL);
	if (!current_process)
	{
		KTEST_END();
		return;
	}
	KASSERT_EQ(sys_pipe(pipefd), (int64_t)0);
	KASSERT_EQ(sys_write(pipefd[1], "AB", 2), (int64_t)2);
	(void)sys_close(pipefd[1]);

	if (is_page_mapped_in_directory(process_pgd(current_process), bad,
					NULL) == 1)
		bad = mm_user_va_start() + 0x00B00000UL;

	saved = current_process->mode;
	current_process->mode = USER_MODE;
	rc = sys_read(pipefd[0], (void *)bad, 2);
	current_process->mode = saved;
	KASSERT_EQ(rc, (int64_t)-EFAULT);

	memset(got, 0, sizeof(got));
	rc = sys_read(pipefd[0], got, 2);
	KASSERT_EQ(rc, (int64_t)2);
	KASSERT(got[0] == 'A' && got[1] == 'B');
	(void)sys_close(pipefd[0]);
	KTEST_END();
}

void ktest_rt_sigsuspend_consumes_pending(void)
{
	process_t *p;
	void (*saved_handler)(int);
	uint32_t saved_pending;
	uint32_t saved_mask;
	uint32_t saved_ignored;
	uint64_t zero_mask = 0;
	int64_t rc;

	KTEST_BEGIN("rt_sigsuspend_consumes_pending");
	p = current_process;
	KASSERT(p != NULL);
	if (!p)
	{
		KTEST_END();
		return;
	}

	saved_handler = p->signal_handlers[SIGUSR1];
	saved_pending = p->signal_pending;
	saved_mask = p->signal_mask;
	saved_ignored = p->signal_ignored;

	/*
	 * Address 2 is neither SIG_DFL nor SIG_IGN nor a user pointer.
	 * Delivery clears the pending bit instead of killing the task.
	 * Returning with the bit still set is the ash spin.
	 */
	p->signal_handlers[SIGUSR1] = (void (*)(int))(uintptr_t)2;
	p->signal_ignored &= ~SIGNAL_MASK(SIGUSR1);
	p->signal_pending = SIGNAL_MASK(SIGUSR1);
	rc = sys_rt_sigsuspend((const sigset_t *)&zero_mask, sizeof(zero_mask));
	KASSERT_EQ(rc, (int64_t)-EINTR);
	KASSERT_EQ(p->signal_pending & SIGNAL_MASK(SIGUSR1), (uint32_t)0);
	KASSERT_EQ(p->signal_mask, saved_mask);
	KASSERT_EQ((unsigned)p->signal_mask_saved_valid, 0u);

	p->signal_handlers[SIGUSR1] = saved_handler;
	p->signal_pending = saved_pending;
	p->signal_mask = saved_mask;
	p->signal_ignored = saved_ignored;
	KTEST_END();
}

static int futex_word;
static int futex_drop_wake;

static void futex_ready_on_sleep(process_t *proc)
{
	proc->syscall_interrupted = 1;
	process_set_sched_state(proc, PROCESS_READY);
}

static void futex_drop_on_sleep(process_t *proc)
{
	ir0_futex_drop_process(proc);
	futex_drop_wake = ir0_futex_wake(&futex_word, 1);
	proc->syscall_interrupted = 1;
	process_set_sched_state(proc, PROCESS_READY);
}

void ktest_futex_interrupted_wait_frees_slot(void)
{
	int rc;

	KTEST_BEGIN("futex_interrupted_wait_frees_slot");
	KASSERT(current_process != NULL);
	if (!current_process)
	{
		KTEST_END();
		return;
	}

	futex_word = 0;
	ir0_futex_before_sleep = futex_ready_on_sleep;
	rc = ir0_futex_wait(current_process, &futex_word, 0);
	ir0_futex_before_sleep = NULL;
	KASSERT_EQ(rc, -EINTR);
	KASSERT_EQ(ir0_futex_wake(&futex_word, 1), 0);
	KTEST_END();
}

void ktest_futex_exit_drop_frees_slot(void)
{
	int rc;

	KTEST_BEGIN("futex_exit_drop_frees_slot");
	KASSERT(current_process != NULL);
	if (!current_process)
	{
		KTEST_END();
		return;
	}

	futex_word = 0;
	futex_drop_wake = -1;
	ir0_futex_before_sleep = futex_drop_on_sleep;
	rc = ir0_futex_wait(current_process, &futex_word, 0);
	ir0_futex_before_sleep = NULL;
	KASSERT_EQ(rc, -EINTR);
	KASSERT_EQ(futex_drop_wake, 0);
	KASSERT_EQ(ir0_futex_wake(&futex_word, 1), 0);
	KTEST_END();
}

static void ipc_fill_on_sleep(ipc_channel_t *channel)
{
	char byte = 'Z';

	(void)ring_buffer_write(&channel->rb, &byte, 1);
}

void ktest_ipc_read_rechecks_after_enqueue(void)
{
	ipc_channel_t *channel;
	char got = 0;
	ssize_t n;

	KTEST_BEGIN("ipc_read_rechecks_after_enqueue");
	KASSERT(current_process != NULL);
	if (!current_process)
	{
		KTEST_END();
		return;
	}

	channel = ipc_channel_create(0x4b544553u);
	KASSERT(channel != NULL);
	if (!channel)
	{
		KTEST_END();
		return;
	}

	channel->writers = 1;
	ipc_sleep_recheck_probe = ipc_fill_on_sleep;
	n = ipc_channel_read(channel, &got, 1);
	ipc_sleep_recheck_probe = NULL;
	KASSERT_EQ(n, (ssize_t)1);
	KASSERT(got == 'Z');
	KASSERT_EQ((unsigned)current_process->state, (unsigned)PROCESS_READY);
	ipc_channel_destroy(channel);
	KTEST_END();
}

void ktest_unmap_splits_2mb_leaf(void)
{
	address_space_root_t root;
	const uintptr_t base = 0x200000UL;

	KTEST_BEGIN("unmap_splits_2mb_leaf");
	root = kmalloc_aligned(PAGE_SIZE_4KB, PAGE_SIZE_4KB);
	KASSERT(root != NULL);
	if (!root)
	{
		KTEST_END();
		return;
	}
	memset(root, 0, PAGE_SIZE_4KB);
	KASSERT_EQ(map_supervisor_identity_2mb(root, base, base + PAGE_SIZE_2MB), 0);
	KASSERT_EQ(is_page_mapped_in_directory(root, base, NULL), 1);
	KASSERT(paging_get_pte(root, base) == NULL);
	KASSERT_EQ(unmap_page_in_directory(root, base), 0);
	KASSERT_EQ(is_page_mapped_in_directory(root, base, NULL), 0);
	KASSERT_EQ(is_page_mapped_in_directory(root, base + PAGE_SIZE_4KB, NULL), 1);
	KASSERT(paging_get_pte(root, base + PAGE_SIZE_4KB) != NULL);
	KTEST_END();
}

void ktest_map_replaced_frame_is_released(void)
{
	address_space_root_t root;
	uintptr_t first;
	uintptr_t second;
	const uint64_t va = 0x800000UL;

	KTEST_BEGIN("map_replaced_frame_is_released");
	root = kmalloc_aligned(PAGE_SIZE_4KB, PAGE_SIZE_4KB);
	KASSERT(root != NULL);
	if (!root)
	{
		KTEST_END();
		return;
	}
	memset(root, 0, PAGE_SIZE_4KB);
	first = pmm_alloc_frame();
	second = pmm_alloc_frame();
	KASSERT(first != 0);
	KASSERT(second != 0);
	KASSERT(first != second);
	if (first == 0 || second == 0)
	{
		KTEST_END();
		return;
	}

	KASSERT_EQ(pmm_frame_refcount(first), 1u);
	KASSERT_EQ(map_page_in_directory(root, va, first, PAGE_RW), 0);
	KASSERT_EQ(map_page_in_directory(root, va, second, PAGE_RW), 0);
	KASSERT_EQ(pmm_frame_refcount(first), 0u);
	KASSERT_EQ(pmm_frame_refcount(second), 1u);
	KASSERT_EQ(map_page_in_directory(root, va, second, PAGE_RW), 0);
	KASSERT_EQ(pmm_frame_refcount(second), 1u);
	KASSERT_EQ(unmap_page_in_directory(root, va), 0);
	KASSERT_EQ(pmm_frame_refcount(second), 0u);
	KTEST_END();
}

static uintptr_t map_fixed_fail_va;

static int map_fixed_unmap_fails_one_page(address_space_root_t root, uintptr_t va)
{
	(void)root;
	if (va == map_fixed_fail_va)
		return -EIO;
	return -EOPNOTSUPP;
}

void ktest_map_fixed_aborts_when_unmap_fails(void)
{
	void *region;
	void *fixed;
	uintptr_t base;
	uint64_t *kept;
	uint64_t kept_pte;
	const int flags = MAP_PRIVATE | MAP_ANONYMOUS;
	const int prot = PROT_READ | PROT_WRITE;

	KTEST_BEGIN("map_fixed_aborts_when_unmap_fails");
	KASSERT(current_process != NULL);
	if (!current_process)
	{
		KTEST_END();
		return;
	}

	region = sys_mmap(NULL, 2 * PAGE_SIZE_4KB, prot, flags, -1, 0);
	KASSERT(region != NULL && region != (void *)(intptr_t)-1);
	if (region == NULL || region == (void *)(intptr_t)-1)
	{
		KTEST_END();
		return;
	}

	base = (uintptr_t)region;
	map_fixed_fail_va = base + PAGE_SIZE_4KB;
	kept = paging_get_pte(process_pgd(current_process), map_fixed_fail_va);
	KASSERT(kept != NULL);
	if (!kept)
	{
		(void)sys_munmap(region, 2 * PAGE_SIZE_4KB);
		KTEST_END();
		return;
	}
	kept_pte = *kept;

	mm_backend_unmap_hook = map_fixed_unmap_fails_one_page;
	fixed = sys_mmap(region, 2 * PAGE_SIZE_4KB, prot, flags | MAP_FIXED, -1, 0);
	mm_backend_unmap_hook = NULL;

	KASSERT(fixed == (void *)(intptr_t)(-ENOMEM));
	KASSERT(is_page_mapped_in_directory(process_pgd(current_process), base,
					    NULL) != 1);
	kept = paging_get_pte(process_pgd(current_process), map_fixed_fail_va);
	KASSERT(kept != NULL);
	if (kept)
		KASSERT_EQ(*kept, kept_pte);

	(void)sys_munmap((void *)map_fixed_fail_va, PAGE_SIZE_4KB);
	KTEST_END();
}

void ktest_shmget_metadata_oom_returns_enomem(void)
{
	int64_t ret;

	KTEST_BEGIN("shmget_metadata_oom_returns_enomem");
	KASSERT_EQ(ktm_fault_configure("kmem.alloc", KTM_FAULT_ONCE, 0), 0);
	ret = sys_shmget(IR0_IPC_PRIVATE, 4096u, 0);
	KASSERT_EQ(ret, (int64_t)-ENOMEM);
	KTEST_END();
}
