/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: lsdrv
 * Copyright (C) 2025 Iván Rodriguez
 *
 * Frontend: reads raw data from /proc/drivers and formats for display.
 * Endpoint only serves data; this binary does all presentation.
 */

#include "debug_bins.h"
#include <stdlib.h>

#define DRV_BUF_SIZE 4096

static int cmd_lsdrv_handler(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    int fd = syscall(SYS_OPEN, (uint64_t)"/proc/drivers", 0, 0);
    if (fd < 0)
    {
        debug_writeln_err("lsdrv: cannot open /proc/drivers");
        return -1;
    }

    char buf[DRV_BUF_SIZE];
    int nr = syscall(SYS_READ, (uint64_t)fd, (uint64_t)buf, sizeof(buf) - 1);
    syscall(SYS_CLOSE, (uint64_t)fd, 0, 0);
    if (nr <= 0)
    {
        debug_writeln("NAME     VERSION LANG STATE DESC");
        return 0;
    }
    buf[nr] = '\0';

    debug_writeln("NAME     VERSION LANG STATE DESC");
    debug_writeln("----------------------------------------");

    const char *p = buf;
    while (*p)
    {
        const char *eol = strchr(p, '\n');
        if (!eol) break;
        if (eol > p)
        {
            char line[256];
            size_t len = (size_t)(eol - p);
            if (len >= sizeof(line)) len = sizeof(line) - 1;
            memcpy(line, p, len);
            line[len] = '\0';
            char name[64], version[32], lang[16], state[32], desc[128];
            name[0] = version[0] = lang[0] = state[0] = desc[0] = '\0';
            char *cur = line;
            struct { char *p; size_t len; } f[] = {
                { name, sizeof(name) }, { version, sizeof(version) },
                { lang, sizeof(lang) }, { state, sizeof(state) }, { desc, sizeof(desc) }
            };
            for (int i = 0; i < 5 && *cur; i++)
            {
                size_t j = 0;
                while (*cur && *cur != '\t' && j < f[i].len - 1)
                    f[i].p[j++] = *cur++;
                f[i].p[j] = '\0';
                if (*cur == '\t') cur++;
            }
            if (name[0])
            {
                char out[280];
                snprintf(out, sizeof(out), "%-8s %-7s %-4s %-10s %s",
                         name, version, lang, state, desc);
                debug_writeln(out);
            }
        }
        p = eol + 1;
    }
    return 0;
}

struct debug_command cmd_lsdrv = {
    .name = "lsdrv",
    .handler = cmd_lsdrv_handler,
    .usage = "lsdrv",
    .description = "List all registered drivers"
};
