/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: chmod
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Change file mode command using POSIX syscalls only
 */

#include "debug_bins.h"
#include <string.h>
#include <stdlib.h>

static int parse_octal_mode(const char *mode_str)
{
    if (!mode_str || *mode_str == '\0')
        return -1;
    
    /* Parse octal mode (e.g., "755", "0644") */
    char *end;
    long mode = strtol(mode_str, &end, 8);
    
    if (*end != '\0' || mode < 0 || mode > 0777)
        return -1;
    
    return (int)mode;
}

static int cmd_chmod_handler(int argc, char **argv)
{
    if (argc < 3)
    {
        debug_write_err("Usage: chmod MODE PATH\n");
        debug_serial_fail("chmod", "usage");
        return 1;
    }
    
    const char *mode_str = argv[1];
    const char *path = argv[2];
    
    int mode = parse_octal_mode(mode_str);
    if (mode < 0)
    {
        debug_write_err("chmod: invalid mode\n");
        debug_serial_fail("chmod", "parse");
        return 1;
    }
    
    /* Use POSIX chmod syscall */
    int64_t result = syscall(SYS_CHMOD, (uint64_t)path, (uint64_t)mode, 0);
    if (result < 0)
    {
        debug_perror("chmod", path, (int)result);
        debug_serial_fail_err("chmod", "syscall", (int)(-result));
        return 1;
    }
    
    debug_serial_ok("chmod");
    return 0;
}

struct debug_command cmd_chmod = {
    .name = "chmod",
    .handler = cmd_chmod_handler,
    .usage = "chmod MODE PATH",
    .description = "Change file mode"
};





