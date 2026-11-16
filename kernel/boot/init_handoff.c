/* SPDX-License-Identifier: GPL-3.0-only */
#include <ir0/init_handoff.h>

long ir0_init_handoff_run(const struct ir0_init_handoff_ops *ops,
			  void *context, const char *path,
			  char *const argv[])
{
	long init_pid;

	if (!ops || !path || !ops->prepare_rootfs || !ops->prepare_init_task ||
	    !ops->load_init || !ops->attach_console || !ops->schedule)
		return IR0_INIT_HANDOFF_BAD_OPS;
	if (ops->prepare_rootfs(context) != 0)
		return IR0_INIT_HANDOFF_ROOTFS_FAILED;
	if (ops->prepare_init_task(context) != 0)
		return IR0_INIT_HANDOFF_TASK_FAILED;
	init_pid = ops->load_init(context, path, argv);
	if (init_pid < 0)
		return IR0_INIT_HANDOFF_LOAD_FAILED;
	if (ops->init_loaded)
		ops->init_loaded(context, init_pid);
	if (ops->spawn_idle)
		(void)ops->spawn_idle(context);
	ops->attach_console(context);
	ops->schedule(context);
	return init_pid;
}
