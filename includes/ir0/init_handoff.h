/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

/**
 * Architecture-neutral transition from kernel boot to the first userspace
 * process.  Backends provide policy-free operations; the common sequencer
 * owns their ordering and failure propagation.
 */
struct ir0_init_handoff_ops
{
	int (*prepare_rootfs)(void *context);
	int (*prepare_init_task)(void *context);
	long (*load_init)(void *context, const char *path, char *const argv[]);
	void (*init_loaded)(void *context, long init_pid);
	int (*spawn_idle)(void *context);
	void (*attach_console)(void *context);
	void (*schedule)(void *context);
};

enum ir0_init_handoff_error
{
	IR0_INIT_HANDOFF_BAD_OPS = -1,
	IR0_INIT_HANDOFF_ROOTFS_FAILED = -2,
	IR0_INIT_HANDOFF_TASK_FAILED = -3,
	IR0_INIT_HANDOFF_LOAD_FAILED = -4,
};

long ir0_init_handoff_run(const struct ir0_init_handoff_ops *ops,
			  void *context, const char *path,
			  char *const argv[]);
