/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: checkpoint.c
 * Description: Lifecycle checkpoints → typed events + transport.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ktm_internal.h>
#include <config.h>

void ktm_checkpoint_emit(ktm_checkpoint_t cp, const char *file, unsigned line)
{
#if defined(CONFIG_KTM) && CONFIG_KTM
	const char *name = "cp";

	(void)file;
	(void)line;
	switch (cp)
	{
	case KTM_CP_BOOT_EARLY:
		name = "BOOT_EARLY";
		break;
	case KTM_CP_BOOT_READY:
		name = "BOOT_READY";
		break;
	case KTM_CP_PROCESS_CREATE:
		name = "PROCESS_CREATE";
		break;
	case KTM_CP_PROCESS_FORK:
		name = "PROCESS_FORK";
		break;
	case KTM_CP_PROCESS_EXEC:
		name = "PROCESS_EXEC";
		break;
	case KTM_CP_PROCESS_EXIT:
		name = "PROCESS_EXIT";
		break;
	case KTM_CP_PROCESS_REAP:
		name = "PROCESS_REAP";
		break;
	case KTM_CP_MM_MAP:
		name = "MM_MAP";
		break;
	case KTM_CP_MM_UNMAP:
		name = "MM_UNMAP";
		break;
	case KTM_CP_MM_FAULT:
		name = "MM_FAULT";
		break;
	case KTM_CP_SCHED_SWITCH:
		name = "SCHED_SWITCH";
		break;
	case KTM_CP_VFS_MOUNT:
		name = "VFS_MOUNT";
		break;
	case KTM_CP_VFS_UMOUNT:
		name = "VFS_UMOUNT";
		break;
	default:
		name = "CHECKPOINT";
		break;
	}
	ktm_event_emit4(KTM_EVENT_CHECKPOINT, KTM_SUBSYS_PROC, (uint64_t)cp, 0, 0, 0);
	ktm_transport_emit("CHECKPOINT", name, NULL);
#else
	(void)cp;
	(void)file;
	(void)line;
#endif
}
