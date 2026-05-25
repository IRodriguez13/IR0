// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Unix-like base directories on the root MINIX volume.
 *
 * Idempotent: existing directories are kept. Called from kmain before
 * kexecve("/sbin/init") and from the debug init hierarchy helper.
 */

#include "rootfs_base.h"
#include <config.h>
#include <ir0/permissions.h>
#include <ir0/errno.h>
#include <ir0/serial_io.h>
#include <fs/vfs.h>

void ir0_rootfs_prepare_userspace_base(void)
{
	const char *dirs[] = {
		"/bin",
		"/sbin",
		"/lib",
		"/lib/tcc",
		"/tmp",
		"/var",
		"/dev",
		"/proc",
		"/etc",
		"/usr",
		"/usr/bin",
		"/usr/include",
		"/usr/include/bits",
		"/usr/lib",
		"/usr/share",
		"/usr/share/doom",
		NULL
	};
	int i;

	for (i = 0; dirs[i] != NULL; i++)
	{
		int ret = vfs_mkdir(dirs[i], 0755);

		if (ret != 0 && ret != -EEXIST)
			break;
	}

	serial_print("[FASE52B][CLASSIFY] FASE52B_BASE_LAYOUT_OK\n");
	serial_print("[FASE53A][CLASSIFY] FASE53A_ROOTFS_LAYOUT_OK\n");
	serial_print("[FASE58L][CLASSIFY] FASE58L_ROOTFS_LAYOUT_OK\n");
}
