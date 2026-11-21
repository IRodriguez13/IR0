/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: sysv_shm.c
 * Description: SysV shmget/shmat/shmdt/shmctl MVP for MIT-SHM prep.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/sysv_shm.h>
#include <ir0/errno.h>
#include <ir0/kmem.h>
#include <ir0/process.h>
#include <ir0/copy_user.h>
#include <ir0/paging.h>
#include <ir0/tlb_sync.h>
#include <ir0/pmm.h>
#include <ir0/arch_port.h>
#include <ir0/clock.h>
#include <string.h>

#define SHM_MAX 8
#define SHM_MAX_PAGES 1024
#define SHM_ATTACH_MAX 16
#define PAGE_SZ 4096u

struct shm_attach
{
	int used;
	process_t *proc;
	uintptr_t va;
	size_t len;
	/* PAGE_USER, or PAGE_USER|PAGE_RW when the attach is writable. */
	uint64_t prot;
};

struct shm_seg
{
	int in_use;
	int key;
	int id;
	int marked_rmid;
	size_t npages;
	uintptr_t *frames;
	int nattch;
	int cpid;
	int lpid;
	int mode;
	time_t atime;
	time_t dtime;
	time_t ctime;
	int uid;
	int gid;
	struct shm_attach attaches[SHM_ATTACH_MAX];
};

static struct shm_seg g_shm[SHM_MAX];
static int g_next_id = 1;

static time_t shm_now(void)
{
	uint64_t uptime_ms = clock_get_uptime_milliseconds();

	if (clock_realtime_available())
		return clock_get_current_time();
	return (time_t)(uptime_ms / 1000);
}

static struct shm_seg *shm_by_id(int shmid)
{
	int i;

	for (i = 0; i < SHM_MAX; i++)
	{
		if (g_shm[i].in_use && g_shm[i].id == shmid)
			return &g_shm[i];
	}
	return NULL;
}

static void shm_free_frames(struct shm_seg *s)
{
	size_t i;

	if (!s || !s->frames)
		return;
	for (i = 0; i < s->npages; i++)
	{
		if (s->frames[i])
			pmm_free_frame(s->frames[i]);
	}
	kfree(s->frames);
	s->frames = NULL;
	s->npages = 0;
}

static void shm_try_destroy(struct shm_seg *s)
{
	if (!s || !s->marked_rmid || s->nattch > 0)
		return;
	shm_free_frames(s);
	memset(s, 0, sizeof(*s));
}

/*
 * One slot per task. sysv_shm_fork copies the parent's slots onto a
 * child with its own page tables; exit and shmdt drop that task's slots
 * once. nattch 0 without IPC_RMID leaves the segment in place.
 */
static void shm_detach_at(struct shm_seg *s, int ai)
{
	struct shm_attach *a;
	address_space_root_t root;
	size_t off;

	a = &s->attaches[ai];
	root = a->proc ? process_pgd(a->proc) : NULL;
	if (root)
	{
		for (off = 0; off < a->len; off += PAGE_SZ)
			unmap_page_in_directory(root, a->va + off);
	}
	a->used = 0;
	a->proc = NULL;
	a->va = 0;
	a->len = 0;
	a->prot = 0;
	if (s->nattch > 0)
		s->nattch--;
	shm_try_destroy(s);
}

void sysv_shm_detach_process(struct process *proc)
{
	int i;
	int ai;

	if (!proc)
		return;
	for (i = 0; i < SHM_MAX; i++)
	{
		struct shm_seg *s = &g_shm[i];

		if (!s->in_use)
			continue;
		for (ai = 0; ai < SHM_ATTACH_MAX; ai++)
		{
			if (!s->in_use)
				break;
			if (!s->attaches[ai].used || s->attaches[ai].proc != proc)
				continue;
			shm_detach_at(s, ai);
		}
	}
}

/*
 * Point one address space at the segment frames with @prot, without
 * taking or dropping a reference and without allocating a frame.
 * copy_process_memory already holds the child's reference on these
 * pages; the parent still holds the reference from shmat.
 */
static int shm_rewire_shared(address_space_root_t root, struct shm_seg *s,
			     uintptr_t va, size_t len, uint64_t prot,
			     int sync_tlb)
{
	size_t off;

	if (!root || !s || !s->frames)
		return -EINVAL;
	for (off = 0; off < len; off += PAGE_SZ)
	{
		size_t pi = off / PAGE_SZ;
		uintptr_t page = va + off;
		uint64_t pte;

		if (pi >= s->npages || !s->frames[pi])
			return -EINVAL;
		pte = mm_make_leaf_pte(s->frames[pi], prot, 0);
		if (paging_install_pte(root, page, pte) != 0)
			return -ENOMEM;
		if (sync_tlb)
			mm_tlb_sync_page(root, page);
	}
	return 0;
}

static int shm_fork_one(struct shm_seg *s, const struct shm_attach *src,
			struct process *child)
{
	int ai;
	uintptr_t va;
	size_t len;
	int rc;

	va = src->va;
	len = src->len;
	for (ai = 0; ai < SHM_ATTACH_MAX; ai++)
	{
		if (!s->attaches[ai].used)
			break;
	}
	if (ai == SHM_ATTACH_MAX)
		return -ENOSPC;

	rc = shm_rewire_shared(process_pgd(child), s, va, len, src->prot, 0);
	if (rc < 0)
		return rc;
	/* Parent TLB still has the COW entry installed by the copy. */
	rc = shm_rewire_shared(process_pgd(src->proc), s, va, len,
			        src->prot, 1);
	if (rc < 0)
		return rc;

	s->attaches[ai].used = 1;
	s->attaches[ai].proc = child;
	s->attaches[ai].va = va;
	s->attaches[ai].len = len;
	s->attaches[ai].prot = src->prot;
	s->nattch++;
	return 0;
}

int sysv_shm_fork(struct process *parent, struct process *child)
{
	int i;
	int ai;

	if (!parent || !child)
		return -EINVAL;
	if (!process_pgd(parent) || !process_pgd(child))
		return -ENOMEM;
	if (process_pgd(parent) == process_pgd(child))
		return 0;

	for (i = 0; i < SHM_MAX; i++)
	{
		struct shm_seg *s = &g_shm[i];

		if (!s->in_use)
			continue;
		for (ai = 0; ai < SHM_ATTACH_MAX; ai++)
		{
			int rc;

			if (!s->attaches[ai].used ||
			    s->attaches[ai].proc != parent)
				continue;
			rc = shm_fork_one(s, &s->attaches[ai], child);
			if (rc < 0)
			{
				sysv_shm_detach_process(child);
				return rc;
			}
		}
	}
	return 0;
}

int64_t sys_shmget(int key, size_t size, int shmflg)
{
	int i;
	struct shm_seg *free_slot = NULL;
	size_t npages;
	size_t p;

	if (size == 0 || size > (size_t)SHM_MAX_PAGES * PAGE_SZ)
		return -EINVAL;
	npages = (size + PAGE_SZ - 1) / PAGE_SZ;

	if (key != IR0_IPC_PRIVATE)
	{
		for (i = 0; i < SHM_MAX; i++)
		{
			if (g_shm[i].in_use && g_shm[i].key == key)
			{
				if ((shmflg & IR0_IPC_CREAT) && (shmflg & IR0_IPC_EXCL))
					return -EEXIST;
				return g_shm[i].id;
			}
		}
		if (!(shmflg & IR0_IPC_CREAT))
			return -ENOENT;
	}

	for (i = 0; i < SHM_MAX; i++)
	{
		if (!g_shm[i].in_use)
		{
			free_slot = &g_shm[i];
			break;
		}
	}
	if (!free_slot)
		return -ENOSPC;

	memset(free_slot, 0, sizeof(*free_slot));
	/* shmget reports ENOMEM; it must not panic merely allocating metadata. */
	free_slot->frames = kcalloc(npages, sizeof(uintptr_t));
	if (!free_slot->frames)
		return -ENOMEM;
	memset(free_slot->frames, 0, npages * sizeof(uintptr_t));
	for (p = 0; p < npages; p++)
	{
		uintptr_t phys = pmm_alloc_frame();

		if (!phys)
		{
			shm_free_frames(free_slot);
			return -ENOMEM;
		}
		free_slot->frames[p] = phys;
		paging_zero_phys_page(phys);
	}
	free_slot->in_use = 1;
	free_slot->key = key;
	free_slot->id = g_next_id++;
	free_slot->npages = npages;
	free_slot->cpid = (int)process_pid(current_process);
	free_slot->mode = (int)(shmflg & 0777);
	free_slot->ctime = shm_now();
	free_slot->uid = current_process ? (int)current_process->euid : 0;
	free_slot->gid = current_process ? (int)current_process->egid : 0;
	return free_slot->id;
}

int64_t sys_shmat(int shmid, const void *shmaddr, int shmflg)
{
	struct shm_seg *s;
	uintptr_t va;
	size_t p;
	int ai;
	address_space_root_t root;
	size_t len;
	uint64_t prot;

	if (!current_process)
		return -ESRCH;
	/* SHM_RND and a chosen address are not implemented. */
	if (shmaddr != NULL || (shmflg & ~IR0_SHM_RDONLY) != 0)
		return -EINVAL;
	prot = PAGE_USER;
	if ((shmflg & IR0_SHM_RDONLY) == 0)
		prot |= PAGE_RW;
	s = shm_by_id(shmid);
	if (!s || !s->frames)
		return -EINVAL;

	root = process_pgd(current_process);
	len = s->npages * PAGE_SZ;
	va = 0x70000000UL + (uintptr_t)((unsigned)shmid * 0x01000000UL) +
	     (uintptr_t)((unsigned)s->nattch * 0x00100000UL);
	for (p = 0; p < s->npages; p++)
	{
		pmm_frame_get(s->frames[p]);
		if (map_page_in_directory(root, va + p * PAGE_SZ, s->frames[p],
					  prot) != 0)
		{
			pmm_frame_put(s->frames[p]);
			while (p > 0)
			{
				p--;
				unmap_page_in_directory(root, va + p * PAGE_SZ);
			}
			return -ENOMEM;
		}
	}
	for (ai = 0; ai < SHM_ATTACH_MAX; ai++)
	{
		if (!s->attaches[ai].used)
		{
			s->attaches[ai].used = 1;
			s->attaches[ai].proc = current_process;
			s->attaches[ai].va = va;
			s->attaches[ai].len = len;
			s->attaches[ai].prot = prot;
			s->nattch++;
			s->lpid = (int)process_pid(current_process);
			s->atime = shm_now();
			return (int64_t)va;
		}
	}
	for (p = 0; p < s->npages; p++)
		unmap_page_in_directory(root, va + p * PAGE_SZ);
	return -ENOSPC;
}

int64_t sys_shmdt(const void *shmaddr)
{
	uintptr_t va = (uintptr_t)shmaddr;
	int i;
	int ai;

	if (!current_process || !shmaddr)
		return -EINVAL;
	for (i = 0; i < SHM_MAX; i++)
	{
		struct shm_seg *s = &g_shm[i];

		if (!s->in_use)
			continue;
		for (ai = 0; ai < SHM_ATTACH_MAX; ai++)
		{
			if (!s->attaches[ai].used ||
			    s->attaches[ai].proc != current_process ||
			    s->attaches[ai].va != va)
				continue;
			shm_detach_at(s, ai);
			s->lpid = (int)process_pid(current_process);
			s->dtime = shm_now();
			return 0;
		}
	}
	return -EINVAL;
}

int64_t sys_shmctl(int shmid, int cmd, void *buf)
{
	struct shm_seg *s = shm_by_id(shmid);
	struct ir0_shmid_ds st;

	if (!s)
		return -EINVAL;
	if (cmd == IR0_IPC_RMID)
	{
		s->marked_rmid = 1;
		shm_try_destroy(s);
		return 0;
	}
	if (cmd == IR0_IPC_STAT)
	{
		if (!buf)
			return -EFAULT;
		memset(&st, 0, sizeof(st));
		st.shm_segsz = s->npages * PAGE_SZ;
		st.shm_nattch = s->nattch;
		st.shm_perm_mode = s->mode;
		st.shm_cpid = s->cpid;
		st.shm_lpid = s->lpid;
		st.shm_atime = s->atime;
		st.shm_dtime = s->dtime;
		st.shm_ctime = s->ctime;
		st.shm_uid = s->uid;
		st.shm_gid = s->gid;
		if (copy_to_user(buf, &st, sizeof(st)) != 0)
			return -EFAULT;
		return 0;
	}
	return -EINVAL;
}
