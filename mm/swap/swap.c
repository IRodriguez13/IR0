/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: swap.c
 * Description: Swap-area table, slot refcounts, and /proc/swaps formatting.
 */

#include "swap_internal.h"

#include <ir0/errno.h>
#include <ir0/kmem.h>
#include <ir0/reclaim.h>
#include <ir0/swap.h>
#include <ir0/validation.h>
#include <string.h>

static struct ir0_swap_area g_areas[IR0_SWAP_MAX_AREAS];
static uint8_t g_lock;
static int g_inited;

static void swap_lock(void)
{
	while (__atomic_test_and_set(&g_lock, __ATOMIC_ACQUIRE))
		__asm__ volatile("" ::: "memory");
}

static void swap_unlock(void)
{
	__atomic_clear(&g_lock, __ATOMIC_RELEASE);
}

static int prio_from_flags(int flags)
{
	if (flags & SWAP_FLAG_PREFER)
		return flags & SWAP_FLAG_PRIO_MASK;
	return 0;
}

static int bitmap_test(const uint8_t *map, uint32_t bit)
{
	return (map[bit / 8U] & (uint8_t)(1U << (bit % 8U))) != 0;
}

static void bitmap_set(uint8_t *map, uint32_t bit)
{
	map[bit / 8U] |= (uint8_t)(1U << (bit % 8U));
}

static void bitmap_clear(uint8_t *map, uint32_t bit)
{
	map[bit / 8U] &= (uint8_t)~(1U << (bit % 8U));
}

struct ir0_swap_area *ir0_swap_area_at(unsigned type)
{
	if (type >= IR0_SWAP_MAX_AREAS || !g_areas[type].used)
		return NULL;
	return &g_areas[type];
}

unsigned ir0_swap_area_count(void)
{
	unsigned n = 0;
	unsigned i;

	for (i = 0; i < IR0_SWAP_MAX_AREAS; i++)
	{
		if (g_areas[i].used)
			n++;
	}
	return n;
}

int ir0_swap_map_lba(const struct ir0_swap_area *area, uint64_t page,
		     uint64_t *lba_out)
{
	size_t i;

	if (!area || !lba_out || page == 0 || page > area->last_page)
		return -EINVAL;
	for (i = 0; i < area->nextent; i++)
	{
		const struct ir0_swap_extent *e = &area->extents[i];
		uint64_t last;

		if (e->nr_pages == 0)
			continue;
		last = e->file_page + e->nr_pages - 1U;
		if (page >= e->file_page && page <= last)
		{
			uint64_t delta = page - e->file_page;

			if (e->phys_lba > UINT64_MAX - delta * 8ULL)
				return -EFBIG;
			*lba_out = e->phys_lba + delta * 8ULL;
			return 0;
		}
	}
	return -ENOENT;
}

static void area_free_meta(struct ir0_swap_area *a)
{
	if (a->bitmap)
		kfree(a->bitmap);
	if (a->refs)
		kfree(a->refs);
	if (a->state)
		kfree(a->state);
	memset(a, 0, sizeof(*a));
}

int ir0_swap_area_install(const char *path, int kind, int flags, dev_t dev,
			  const struct ir0_swap_extent *extents, size_t nextent,
			  uint32_t last_page)
{
	struct ir0_swap_area *slot = NULL;
	size_t bitmap_bytes;
	unsigned i;
	int prio = prio_from_flags(flags);

	if (!path || path[0] == '\0' || !extents || nextent == 0 ||
	    nextent > IR0_SWAP_MAX_EXTENTS)
		return -EINVAL;
	if (kind != IR0_SWAP_KIND_BLOCK && kind != IR0_SWAP_KIND_FILE)
		return -EINVAL;
	if (last_page == 0 || last_page >= IR0_SWAP_MAX_PAGES)
		return -EINVAL;
	if (dev == 0)
		return -ENODEV;

	swap_lock();
	for (i = 0; i < IR0_SWAP_MAX_AREAS; i++)
	{
		if (g_areas[i].used && strcmp(g_areas[i].path, path) == 0)
		{
			swap_unlock();
			return -EBUSY;
		}
	}
	for (i = 0; i < IR0_SWAP_MAX_AREAS; i++)
	{
		if (!g_areas[i].used)
		{
			slot = &g_areas[i];
			break;
		}
	}
	if (!slot)
	{
		swap_unlock();
		return -EPERM;
	}

	memset(slot, 0, sizeof(*slot));
	bitmap_bytes = ((size_t)last_page + 1U + 7U) / 8U;
	slot->bitmap = kmalloc_try(bitmap_bytes);
	slot->refs = kmalloc_try(((size_t)last_page + 1U) * sizeof(uint16_t));
	slot->state = kmalloc_try((size_t)last_page + 1U);
	if (!slot->bitmap || !slot->refs || !slot->state)
	{
		area_free_meta(slot);
		swap_unlock();
		return -ENOMEM;
	}
	memset(slot->bitmap, 0, bitmap_bytes);
	memset(slot->refs, 0, ((size_t)last_page + 1U) * sizeof(uint16_t));
	memset(slot->state, 0, (size_t)last_page + 1U);
	bitmap_set(slot->bitmap, 0);
	slot->refs[0] = 1;
	slot->kind = kind;
	slot->prio = prio;
	slot->dev = dev;
	slot->last_page = last_page;
	slot->nextent = nextent;
	memcpy(slot->extents, extents, nextent * sizeof(*extents));
	strncpy(slot->path, path, sizeof(slot->path) - 1U);
	slot->used = 1;
	swap_unlock();
	return 0;
}

int ir0_swap_area_remove(const char *path)
{
	unsigned i;
	int rc;

	if (!path)
		return -EINVAL;
	swap_lock();
	for (i = 0; i < IR0_SWAP_MAX_AREAS; i++)
	{
		if (!g_areas[i].used || strcmp(g_areas[i].path, path) != 0)
			continue;
		if (g_areas[i].inuse != 0)
		{
			swap_unlock();
			rc = ir0_swap_drain_area(i);
			swap_lock();
			if (rc < 0 && g_areas[i].inuse != 0)
			{
				swap_unlock();
				return rc;
			}
		}
		area_free_meta(&g_areas[i]);
		swap_unlock();
		return 0;
	}
	swap_unlock();
	return -EINVAL;
}

int ir0_swap_path_busy(const char *path)
{
	unsigned i;
	int busy = 0;

	if (!path)
		return 0;
	swap_lock();
	for (i = 0; i < IR0_SWAP_MAX_AREAS; i++)
	{
		if (g_areas[i].used && strcmp(g_areas[i].path, path) == 0)
		{
			busy = 1;
			break;
		}
	}
	swap_unlock();
	return busy;
}

int ir0_swap_slot_alloc(unsigned *type, uint64_t *offset)
{
	int best_prio = -1;
	unsigned best = IR0_SWAP_MAX_AREAS;
	unsigned i;
	uint32_t page;

	if (!type || !offset)
		return -EINVAL;
	swap_lock();
	for (i = 0; i < IR0_SWAP_MAX_AREAS; i++)
	{
		if (!g_areas[i].used)
			continue;
		if (best == IR0_SWAP_MAX_AREAS || g_areas[i].prio > best_prio)
		{
			best = i;
			best_prio = g_areas[i].prio;
		}
	}
	if (best == IR0_SWAP_MAX_AREAS)
	{
		swap_unlock();
		return -ENOSPC;
	}
	for (page = 1; page <= g_areas[best].last_page; page++)
	{
		if (!bitmap_test(g_areas[best].bitmap, page))
		{
			bitmap_set(g_areas[best].bitmap, page);
			g_areas[best].refs[page] = 1;
			g_areas[best].inuse++;
			*type = best;
			*offset = page;
			swap_unlock();
			return 0;
		}
	}
	swap_unlock();
	return -ENOSPC;
}

int ir0_swap_slot_get(unsigned type, uint64_t offset)
{
	if (type >= IR0_SWAP_MAX_AREAS || !g_areas[type].used)
		return -EINVAL;
	if (offset == 0 || offset > g_areas[type].last_page)
		return -EINVAL;
	swap_lock();
	if (!bitmap_test(g_areas[type].bitmap, (uint32_t)offset))
	{
		swap_unlock();
		return -ENOENT;
	}
	if (g_areas[type].refs[offset] == 0xffff)
	{
		swap_unlock();
		return -ENOMEM;
	}
	g_areas[type].refs[offset]++;
	swap_unlock();
	return 0;
}

int ir0_swap_slot_put(unsigned type, uint64_t offset)
{
	if (type >= IR0_SWAP_MAX_AREAS || !g_areas[type].used)
		return -EINVAL;
	if (offset == 0 || offset > g_areas[type].last_page)
		return -EINVAL;
	swap_lock();
	if (g_areas[type].refs[offset] == 0)
	{
		swap_unlock();
		return -EINVAL;
	}
	g_areas[type].refs[offset]--;
	if (g_areas[type].refs[offset] == 0)
	{
		bitmap_clear(g_areas[type].bitmap, (uint32_t)offset);
		if (g_areas[type].inuse > 0)
			g_areas[type].inuse--;
	}
	swap_unlock();
	return 0;
}

int ir0_swap_slot_busy(unsigned type)
{
	if (type >= IR0_SWAP_MAX_AREAS || !g_areas[type].used)
		return 0;
	return g_areas[type].inuse != 0;
}

void ir0_swap_get_stats(struct ir0_swap_stats *out)
{
	unsigned i;

	if (!out)
		return;
	memset(out, 0, sizeof(*out));
	swap_lock();
	for (i = 0; i < IR0_SWAP_MAX_AREAS; i++)
	{
		if (!g_areas[i].used)
			continue;
		out->areas++;
		out->totalswap_pages += g_areas[i].last_page;
		out->freeswap_pages += g_areas[i].last_page - g_areas[i].inuse;
	}
	swap_unlock();
}

static size_t swap_append(char *buf, size_t cap, size_t off, const char *s)
{
	while (s && *s && off + 1U < cap)
		buf[off++] = *s++;
	return off;
}

static size_t swap_append_int(char *buf, size_t cap, size_t off, int value)
{
	char tmp[16];
	unsigned n = 0;
	unsigned mag;
	int neg = 0;

	if (value < 0)
	{
		neg = 1;
		mag = (unsigned)(-value);
	}
	else
		mag = (unsigned)value;
	if (mag == 0)
		tmp[n++] = '0';
	while (mag && n < sizeof(tmp))
	{
		tmp[n++] = (char)('0' + (mag % 10U));
		mag /= 10U;
	}
	if (neg && off + 1U < cap)
		buf[off++] = '-';
	while (n && off + 1U < cap)
		buf[off++] = tmp[--n];
	return off;
}

int ir0_swap_format_proc(char *buf, size_t count)
{
	static const char hdr[] =
		"Filename\t\tType\t\tSize\t\tUsed\t\tPriority\n";
	size_t off;
	unsigned i;

	if (VALIDATE_BUFFER(buf, count) != 0)
		return -1;
	memset(buf, 0, count);
	if (count <= sizeof(hdr))
	{
		memcpy(buf, hdr, count - 1U);
		buf[count - 1U] = '\0';
		return (int)(count - 1U);
	}
	memcpy(buf, hdr, sizeof(hdr) - 1U);
	off = sizeof(hdr) - 1U;
	swap_lock();
	for (i = 0; i < IR0_SWAP_MAX_AREAS && off + 80U < count; i++)
	{
		const char *type;

		if (!g_areas[i].used)
			continue;
		type = g_areas[i].kind == IR0_SWAP_KIND_FILE ? "file" : "partition";
		off = swap_append(buf, count, off, g_areas[i].path);
		off = swap_append(buf, count, off, "\t");
		off = swap_append(buf, count, off, type);
		off = swap_append(buf, count, off, "\t");
		off = swap_append_int(buf, count, off,
				      (int)(g_areas[i].last_page * 4U));
		off = swap_append(buf, count, off, "\t");
		off = swap_append_int(buf, count, off,
				      (int)(g_areas[i].inuse * 4U));
		off = swap_append(buf, count, off, "\t");
		off = swap_append_int(buf, count, off, g_areas[i].prio);
		off = swap_append(buf, count, off, "\n");
	}
	swap_unlock();
	if (off >= count)
		off = count - 1U;
	buf[off] = '\0';
	return (int)off;
}

static uint64_t swap_reclaim_count(void *ctx)
{
	struct ir0_swap_stats st;

	(void)ctx;
	ir0_swap_get_stats(&st);
	return st.freeswap_pages;
}

static int swap_reclaim_scan(void *ctx, uint64_t target, uint64_t *reclaimed)
{
	(void)ctx;
	if (!reclaimed)
		return -EINVAL;
	*reclaimed = 0;
	if (target == 0)
		return -EINVAL;
	return ir0_swap_try_reclaim(target, reclaimed);
}

static const struct ir0_reclaimer g_swap_reclaimer = {
	.name = "anon-swap",
	.count = swap_reclaim_count,
	.scan = swap_reclaim_scan,
	.ctx = NULL,
};

void ir0_swap_init(void)
{
	if (g_inited)
		return;
	memset(g_areas, 0, sizeof(g_areas));
	(void)mm_reclaimer_register(&g_swap_reclaimer);
	g_inited = 1;
}
