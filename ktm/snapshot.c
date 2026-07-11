/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: snapshot.c
 * Description: System snapshot + builtin mm/proc probes.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "ktm_internal.h"
#include <ir0/process.h>
#include <mm/pmm.h>
#include <string.h>

extern process_t *process_list;
extern process_t *current_process;

static int probe_mm_frames(void *ctx, ktm_writer_t *w)
{
	size_t total = 0, used = 0, free_f = 0;

	(void)ctx;
	pmm_stats(&total, &used, &free_f);
	ktm_write_u64(w, "mm.frames.total", total);
	ktm_write_u64(w, "mm.frames.used", used);
	ktm_write_u64(w, "mm.frames.free", free_f);
	return 0;
}

static int probe_proc_list(void *ctx, ktm_writer_t *w)
{
	process_t *p;
	uint64_t n = 0, z = 0;

	(void)ctx;
	for (p = process_list; p; p = p->next)
	{
		n++;
		if (p->state == PROCESS_ZOMBIE)
			z++;
	}
	ktm_write_u64(w, "proc.list.count", n);
	ktm_write_u64(w, "proc.list.zombies", z);
	return 0;
}

void ktm_probes_register_builtins(void)
{
	(void)ktm_probe_register("mm.frames", probe_mm_frames, NULL);
	(void)ktm_probe_register("proc.list", probe_proc_list, NULL);
}

int ktm_snapshot_take(ktm_system_snapshot_t *out)
{
	process_t *p;
	size_t total = 0, used = 0, free_f = 0;
	uint64_t n = 0, z = 0, fds = 0;
	int i;

	if (!out)
		return -1;
	memset(out, 0, sizeof(*out));
	pmm_stats(&total, &used, &free_f);
	out->total_frames = total;
	out->used_frames = used;
	out->free_frames = free_f;

	for (p = process_list; p; p = p->next)
	{
		n++;
		if (p->state == PROCESS_ZOMBIE)
			z++;
		for (i = 0; i < MAX_FDS_PER_PROCESS; i++)
		{
			if (p->fd_table[i].in_use)
			{
				fds++;
				if (p->fd_table[i].is_pipe)
					out->pipes++;
			}
		}
	}
	out->processes = n;
	out->zombies = z;
	out->open_fds = fds;
	return 0;
}

void ktm_snapshot_diff(const ktm_system_snapshot_t *before,
		       const ktm_system_snapshot_t *after,
		       ktm_snapshot_delta_t *delta)
{
	if (!before || !after || !delta)
		return;
	delta->used_frames = (int64_t)after->used_frames - (int64_t)before->used_frames;
	delta->processes = (int64_t)after->processes - (int64_t)before->processes;
	delta->zombies = (int64_t)after->zombies - (int64_t)before->zombies;
	delta->open_fds = (int64_t)after->open_fds - (int64_t)before->open_fds;
	delta->pipes = (int64_t)after->pipes - (int64_t)before->pipes;
}
