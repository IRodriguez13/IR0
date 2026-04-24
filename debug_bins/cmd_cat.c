/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: cat
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Print file contents command (uses only syscalls)
 */

#include "debug_bins.h"
#include <ir0/fcntl.h>

static int cmd_cat_handler(int argc, char **argv)
{
    if (argc < 2)
    {
        debug_write_err("Usage: cat <filename>\n");
        debug_serial_fail("cat", "usage");
        return -1;
    }

    const char *filename = argv[1];
    int fd = ir0_open(filename, O_RDONLY, 0);

    if (fd < 0)
    {
        debug_perror("cat", filename, (int)fd);
        debug_serial_fail_err("cat", "open", (int)(-fd));
        return -1;
    }

    char buffer[512];
    int max_iterations = 1000; /* Safety limit */
    int iteration = 0;
    int failed = 0;

    for (;;)
    {
        if (iteration >= max_iterations)
        {
            debug_write_err("cat: too many iterations, possible infinite loop\n");
            debug_serial_fail("cat", "loop");
            failed = 1;
            break;
        }

        int64_t bytes_read = ir0_read(fd, buffer, sizeof(buffer));

        if (bytes_read < 0)
        {
            debug_perror("cat", filename, (int)bytes_read);
            debug_serial_fail_err("cat", "read", (int)(-bytes_read));
            failed = 1;
            break;
        }
        if (bytes_read == 0)
            break;

        int64_t bytes_written = syscall(SYS_WRITE, 1, (uint64_t)buffer, (uint64_t)bytes_read);

        if (bytes_written < 0)
        {
            debug_perror("cat", "stdout", (int)bytes_written);
            debug_serial_fail_err("cat", "write", (int)(-bytes_written));
            failed = 1;
            break;
        }
        if (bytes_written < bytes_read)
        {
            char written_str[32];
            char read_str[32];
            char wbuf[120];
            debug_u64_to_dec((uint64_t)bytes_written, written_str, sizeof(written_str));
            debug_u64_to_dec((uint64_t)bytes_read, read_str, sizeof(read_str));

            snprintf(wbuf, sizeof(wbuf),
                     "cat: warning: partial write to stdout (%s of %s bytes)\n",
                     written_str, read_str);
            debug_write_err(wbuf);
            debug_serial_fail_err("cat", "write_partial", (int)EIO);
            failed = 1;
            break;
        }

        iteration++;
    }

    ir0_close(fd);

    if (!failed)
        debug_serial_ok("cat");

    return failed ? -1 : 0;
}

struct debug_command cmd_cat = {
    .name = "cat",
    .handler = cmd_cat_handler,
    .usage = "cat FILE",
    .description = "Print file contents"
};
