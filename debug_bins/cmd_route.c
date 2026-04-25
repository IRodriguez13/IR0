/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - route command (debug bin)
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Show route-oriented summary from /dev/net.
 */

#include "debug_bins.h"

static int read_text_file(const char *path, char *buf, size_t buf_sz)
{
    int fd;
    int64_t rd;

    if (!path || !buf || buf_sz == 0)
        return -EINVAL;

    fd = (int)syscall(SYS_OPEN, (uint64_t)path, O_RDONLY, 0);
    if (fd < 0)
        return fd;

    rd = syscall(SYS_READ, (uint64_t)fd, (uint64_t)buf, (uint64_t)(buf_sz - 1));
    syscall(SYS_CLOSE, (uint64_t)fd, 0, 0);
    if (rd < 0)
        return (int)rd;

    buf[(size_t)rd] = '\0';
    return (int)rd;
}

static int copy_field_after(const char *src, const char *tag, char *out, size_t out_sz)
{
    const char *p;
    size_t n = 0;

    if (!src || !tag || !out || out_sz == 0)
        return -EINVAL;

    p = strstr(src, tag);
    if (!p)
        return -ENOENT;
    p += strlen(tag);
    while (*p == ' ' || *p == '\t')
        p++;

    while (*p && *p != ' ' && *p != '\t' && *p != '\n' && n + 1 < out_sz)
        out[n++] = *p++;
    out[n] = '\0';
    return (n > 0) ? 0 : -EINVAL;
}

static int route_parse_iface(const char *netdev, char *iface, size_t iface_sz)
{
    const char *p = netdev;

    if (!netdev || !iface || iface_sz == 0)
        return -EINVAL;

    while (*p)
    {
        while (*p == '\n')
            p++;
        if (*p == '\0')
            break;

        const char *line_end = strchr(p, '\n');
        if (!line_end)
            line_end = p + strlen(p);

        if (strchr(p, ':') && strncmp(p, "Inter-|", 7) != 0 && strncmp(p, " face |", 7) != 0)
        {
            const char *colon = strchr(p, ':');
            const char *name_start = p;
            while (name_start < colon && (*name_start == ' ' || *name_start == '\t'))
                name_start++;

            size_t len = (size_t)(colon - name_start);
            if (len > 0 && len + 1 < iface_sz)
            {
                memcpy(iface, name_start, len);
                iface[len] = '\0';
                return 0;
            }
        }

        p = (*line_end == '\0') ? line_end : line_end + 1;
    }

    return -ENOENT;
}

static int cmd_route_handler(int argc, char **argv)
{
    char devnet[2048];
    char netdev[1536];
    char ip[64];
    char mask[64];
    char gw[64];
    char iface[32];
    char line[256];
    int rd_devnet;
    int rd_netdev;

    if (argc > 1)
    {
        debug_writeln_err("usage: route");
        return 1;
    }
    (void)argv;

    rd_devnet = read_text_file("/dev/net", devnet, sizeof(devnet));
    if (rd_devnet < 0)
    {
        debug_perror("route", "/dev/net", rd_devnet);
        return 1;
    }
    rd_netdev = read_text_file("/proc/net/dev", netdev, sizeof(netdev));
    if (rd_netdev < 0)
    {
        debug_perror("route", "/proc/net/dev", rd_netdev);
        return 1;
    }

    if (route_parse_iface(netdev, iface, sizeof(iface)) != 0)
        strcpy(iface, "eth0");

    if (strstr(devnet, "type=ping_result") != NULL)
    {
        debug_writeln("route: /dev/net reports last ping result; retry for route snapshot");
        return 0;
    }

    if (copy_field_after(devnet, "ip=", ip, sizeof(ip)) != 0 ||
        copy_field_after(devnet, "netmask=", mask, sizeof(mask)) != 0 ||
        copy_field_after(devnet, "gateway=", gw, sizeof(gw)) != 0)
    {
        debug_writeln_err("route: could not parse route fields from /dev/net");
        debug_writeln_err("hint: run ndev to inspect raw net node output");
        return 1;
    }

    debug_writeln("Kernel route summary");
    snprintf(line, sizeof(line), "default via %s dev %s", gw, iface);
    debug_writeln(line);
    snprintf(line, sizeof(line), "connected %s mask %s dev %s", ip, mask, iface);
    debug_writeln(line);
    return 0;
}

struct debug_command cmd_route = {
    .name = "route",
    .handler = cmd_route_handler,
    .usage = "route",
    .description = "Show route summary from /dev/net",
    .flags = NULL
};

