/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: ps
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Frontend: reads raw data from /proc/ps and formats for display.
 * Endpoint only serves data; this binary does all presentation.
 */

#include "debug_bins.h"
#include <stdlib.h>

#define PS_BUF_SIZE 2048

static int cmd_ps_handler(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    int fd = syscall(SYS_OPEN, (uint64_t)"/proc/ps", 0, 0);
    if (fd < 0)
    {
        debug_writeln_err("ps: cannot open /proc/ps");
        debug_serial_fail("ps", "open");
        return -1;
    }

    char buf[PS_BUF_SIZE];
    int nr = syscall(SYS_READ, (uint64_t)fd, (uint64_t)buf, sizeof(buf) - 1);
    syscall(SYS_CLOSE, (uint64_t)fd, 0, 0);
    if (nr <= 0)
    {
        debug_writeln("PID PPID STATE NAME");
        return 0;
    }
    buf[nr] = '\0';

    /* OSDev-style header: PID PPID S UID CMD */
    debug_writeln("  PID  PPID  S   UID  CMD");
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
            int pid = 0, ppid = 0, uid = 0;
            char state[8], name[64];
            state[0] = name[0] = '\0';
            char *cur = line;
            if (*cur)
            {
                pid = atoi(cur);
                while (*cur && *cur != '\t') cur++;
                if (*cur == '\t') cur++;
                if (*cur) { ppid = atoi(cur); while (*cur && *cur != '\t') cur++; if (*cur == '\t') cur++; }
                if (*cur) { size_t i = 0; while (*cur && *cur != '\t' && i < sizeof(state)-1) state[i++] = *cur++; state[i] = '\0'; if (*cur == '\t') cur++; }
                if (*cur) { uid = atoi(cur); while (*cur && *cur != '\t') cur++; if (*cur == '\t') cur++; }
                if (*cur) { size_t i = 0; while (*cur && *cur != '\n' && i < sizeof(name)-1) name[i++] = *cur++; name[i] = '\0'; }
            }
            if (name[0] || pid != 0)
            {
                char out[180];
                char state_char = state[0] ? state[0] : '?';
                snprintf(out, sizeof(out), "%5d %5d  %c %5d  %s", pid, ppid, state_char, uid, name[0] ? name : "(none)");
                debug_writeln(out);
            }
        }
        p = eol + 1;
    }
    debug_serial_ok("ps");
    return 0;
}

struct debug_command cmd_ps = {
    .name = "ps",
    .handler = cmd_ps_handler,
    .usage = "ps",
    .description = "List processes"
};
