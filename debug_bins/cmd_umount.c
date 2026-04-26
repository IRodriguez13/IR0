/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: umount
 * Unmount filesystem from a mountpoint path.
 */

#include "debug_bins.h"
#include <string.h>

static const char *const umount_flags[] = {
    "-h",
    "--help",
    NULL
};

static void umount_print_usage(void)
{
    debug_writeln("usage:");
    debug_writeln("  umount MOUNTPOINT");
    debug_writeln("");
    debug_writeln("example:");
    debug_writeln("  umount /mnt/tmp");
}

static int cmd_umount_handler(int argc, char **argv)
{
    const char *target;
    int64_t ret;

    if (argc >= 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0))
    {
        umount_print_usage();
        debug_serial_ok("umount");
        return 0;
    }

    if (argc != 2)
    {
        umount_print_usage();
        debug_serial_fail("umount", "usage");
        return 1;
    }

    target = argv[1];
    ret = syscall(SYS_UMOUNT, (uint64_t)target, 0, 0);
    if (ret < 0)
    {
        debug_perror("umount", target, (int)ret);
        debug_serial_fail_err("umount", "sys_umount", (int)ret);
        return 1;
    }

    debug_serial_ok("umount");
    return 0;
}

struct debug_command cmd_umount = {
    .name = "umount",
    .handler = cmd_umount_handler,
    .usage = "umount MOUNTPOINT",
    .description = "Unmount filesystem by mountpoint",
    .flags = umount_flags
};

