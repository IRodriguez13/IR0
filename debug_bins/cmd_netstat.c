/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - netstat command (debug bin)
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Minimal network statistics view for debug shell.
 * Reads Linux-style /proc/net/dev exported by procfs.
 */

#include "debug_bins.h"

static void trim_copy(const char *src, char *dst, size_t dst_sz)
{
    size_t n = 0;

    if (!src || !dst || dst_sz == 0)
        return;

    while (*src == ' ' || *src == '\t')
        src++;
    while (*src && *src != '\n' && *src != '\r' && n + 1 < dst_sz)
        dst[n++] = *src++;
    while (n > 0 && (dst[n - 1] == ' ' || dst[n - 1] == '\t'))
        n--;
    dst[n] = '\0';
}

static int split_ws_fields(char *line, char **fields, int max_fields)
{
    int count = 0;
    char *p = line;

    while (*p && count < max_fields)
    {
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '\0')
            break;
        fields[count++] = p;
        while (*p && *p != ' ' && *p != '\t')
            p++;
        if (*p == '\0')
            break;
        *p++ = '\0';
    }

    return count;
}

static int cmd_netstat_handler(int argc, char **argv)
{
    char buf[1536];
    const char *p;
    int fd;
    int64_t rd;

    if (argc > 1)
    {
        debug_writeln_err("netstat: this implementation does not take arguments");
        debug_writeln_err("usage: netstat");
        return 1;
    }
    (void)argv;

    fd = (int)syscall(SYS_OPEN, (uint64_t)"/proc/net/dev", O_RDONLY, 0);
    if (fd < 0)
    {
        debug_perror("netstat", "/proc/net/dev", fd);
        return 1;
    }

    rd = syscall(SYS_READ, (uint64_t)fd, (uint64_t)buf, (uint64_t)(sizeof(buf) - 1));
    syscall(SYS_CLOSE, (uint64_t)fd, 0, 0);
    if (rd < 0)
    {
        debug_perror("netstat", "/proc/net/dev", (int)rd);
        return 1;
    }

    buf[(size_t)rd] = '\0';
    p = buf;

    debug_writeln("Iface      RX packets   RX err   TX packets   TX err");
    debug_writeln("------------------------------------------------------");
    while (*p)
    {
        char line[256];
        const char *line_end = strchr(p, '\n');
        size_t len = line_end ? (size_t)(line_end - p) : strlen(p);

        if (len >= sizeof(line))
            len = sizeof(line) - 1;
        memcpy(line, p, len);
        line[len] = '\0';

        char *colon = strchr(line, ':');
        if (colon && strncmp(line, "Inter-|", 7) != 0 && strncmp(line, " face |", 7) != 0)
        {
            char ifname[32];
            char *stats = colon + 1;
            char *fields[8];
            int nfields;
            char rxp[24] = "0";
            char rxe[24] = "0";
            char txp[24] = "0";
            char txe[24] = "0";
            char out[192];

            *colon = '\0';
            trim_copy(line, ifname, sizeof(ifname));

            nfields = split_ws_fields(stats, fields, 8);
            if (nfields > 0)
                strncpy(rxp, fields[0], sizeof(rxp) - 1);
            if (nfields > 1)
                strncpy(rxe, fields[1], sizeof(rxe) - 1);
            if (nfields > 2)
                strncpy(txp, fields[2], sizeof(txp) - 1);
            if (nfields > 3)
                strncpy(txe, fields[3], sizeof(txe) - 1);

            rxp[sizeof(rxp) - 1] = '\0';
            rxe[sizeof(rxe) - 1] = '\0';
            txp[sizeof(txp) - 1] = '\0';
            txe[sizeof(txe) - 1] = '\0';

            snprintf(out, sizeof(out), "%-10s %-12s %-8s %-12s %s",
                     ifname, rxp, rxe, txp, txe);
            debug_writeln(out);
        }

        if (!line_end)
            break;
        p = line_end + 1;
    }

    return 0;
}

struct debug_command cmd_netstat = {
    .name = "netstat",
    .handler = cmd_netstat_handler,
    .usage = "netstat",
    .description = "Show compact interface packet/error statistics",
    .flags = NULL
};

