/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: snapshot.c
 * Description: System snapshot + builtin mm/proc probes.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ktm_internal.h>
#include <ir0/mm_port.h>
#include <ir0/process_introspect.h>
#include <string.h>

static int probe_mm_frames(void *ctx, ktm_writer_t *w)
{
	size_t total = 0;
	size_t used = 0;
	size_t free_f = 0;

	(void)ctx;
	ir0_mm_pmm_stats(&total, &used, &free_f);
	ktm_write_u64(w, "mm.frames.total", total);
	ktm_write_u64(w, "mm.frames.used", used);
	ktm_write_u64(w, "mm.frames.free", free_f);
	return 0;
}

static int probe_proc_list(void *ctx, ktm_writer_t *w)
{
	ir0_proc_snapshot_t snap;

	(void)ctx;
	if (ir0_proc_snapshot_collect(&snap) != 0)
		return -1;
	ktm_write_u64(w, "proc.list.count", snap.processes);
	ktm_write_u64(w, "proc.list.zombies", snap.zombies);
	return 0;
}

void ktm_probes_register_builtins(void)
{
	(void)ktm_probe_register("mm.frames", probe_mm_frames, NULL);
	(void)ktm_probe_register("proc.list", probe_proc_list, NULL);
}

int ktm_snapshot_take(ktm_system_snapshot_t *out)
{
	ir0_proc_snapshot_t proc;
	size_t total = 0;
	size_t used = 0;
	size_t free_f = 0;

	if (!out)
		return -1;
	memset(out, 0, sizeof(*out));
	ir0_mm_pmm_stats(&total, &used, &free_f);
	out->total_frames = total;
	out->used_frames = used;
	out->free_frames = free_f;

	if (ir0_proc_snapshot_collect(&proc) != 0)
		return -1;
	out->processes = proc.processes;
	out->zombies = proc.zombies;
	out->open_fds = proc.open_fds;
	out->pipes = proc.pipes;
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
