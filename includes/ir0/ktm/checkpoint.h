/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: checkpoint.h
 * Description: Neutral lifecycle checkpoints (replace legacy serial audits).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <config.h>

typedef enum ktm_checkpoint
{
	KTM_CP_BOOT_EARLY = 1,
	KTM_CP_BOOT_READY,
	KTM_CP_PROCESS_CREATE,
	KTM_CP_PROCESS_FORK,
	KTM_CP_PROCESS_EXEC,
	KTM_CP_PROCESS_EXIT,
	KTM_CP_PROCESS_REAP,
	KTM_CP_MM_MAP,
	KTM_CP_MM_UNMAP,
	KTM_CP_MM_FAULT,
	KTM_CP_SCHED_SWITCH,
	KTM_CP_VFS_MOUNT,
	KTM_CP_VFS_UMOUNT
} ktm_checkpoint_t;

void ktm_checkpoint_emit(ktm_checkpoint_t cp, const char *file, unsigned line);

#if defined(CONFIG_KTM) && CONFIG_KTM
#define KTM_CHECKPOINT(cp) ktm_checkpoint_emit((cp), __FILE__, __LINE__)
#else
#define KTM_CHECKPOINT(cp) do { } while (0)
#endif
