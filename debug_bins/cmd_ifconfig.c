/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - ifconfig command (debug bin)
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Minimal network interface view for debug shell.
 * Data source is procfs via syscalls only.
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

static int split_tab_fields(char *line, char **fields, int max_fields)
{
    int count = 0;
    char *p = line;

    while (count < max_fields && p && *p)
    {
        fields[count++] = p;
        p = strchr(p, '\t');
        if (!p)
            break;
        *p = '\0';
        p++;
    }

    return count;
}

static int parse_netdev_stats(const char *netdev, const char *iface,
                              char *rx_pkt, size_t rx_sz,
                              char *rx_err, size_t rxe_sz,
                              char *tx_pkt, size_t tx_sz,
                              char *tx_err, size_t txe_sz)
{
    const char *p = netdev;
    char line[256];

    if (!netdev || !iface)
        return -EINVAL;

    while (*p)
    {
        const char *line_end = strchr(p, '\n');
        size_t len = line_end ? (size_t)(line_end - p) : strlen(p);
        if (len >= sizeof(line))
            len = sizeof(line) - 1;
        memcpy(line, p, len);
        line[len] = '\0';

        char *colon = strchr(line, ':');
        if (colon)
        {
            *colon = '\0';
            char ifname[32];
            trim_copy(line, ifname, sizeof(ifname));

            if (strcmp(ifname, iface) == 0)
            {
                char *stats = colon + 1;
                char *fields[8];
                int nfields;
                char rxp[24] = "0";
                char rxe[24] = "0";
                char txp[24] = "0";
                char txe[24] = "0";

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

                strncpy(rx_pkt, rxp, rx_sz - 1);
                rx_pkt[rx_sz - 1] = '\0';
                strncpy(rx_err, rxe, rxe_sz - 1);
                rx_err[rxe_sz - 1] = '\0';
                strncpy(tx_pkt, txp, tx_sz - 1);
                tx_pkt[tx_sz - 1] = '\0';
                strncpy(tx_err, txe, txe_sz - 1);
                tx_err[txe_sz - 1] = '\0';
                return 0;
            }
        }

        if (!line_end)
            break;
        p = line_end + 1;
    }

    return -ENOENT;
}

static void print_ifconfig_pretty(const char *netinfo, const char *netdev)
{
    const char *p = netinfo;
    int printed = 0;

    debug_writeln("Interface  MTU    Flags                MAC");
    debug_writeln("---------------------------------------------------------------");
    while (*p)
    {
        char line[256];
        const char *line_end = strchr(p, '\n');
        size_t len = line_end ? (size_t)(line_end - p) : strlen(p);
        if (len >= sizeof(line))
            len = sizeof(line) - 1;
        memcpy(line, p, len);
        line[len] = '\0';

        if (line[0] != '\0')
        {
            char *fields[4];
            int nfields = split_tab_fields(line, fields, 4);
            if (nfields == 4)
            {
                char name[32];
                char mtu[16];
                char flags[64];
                char mac[32];
                char l1[256];
                char l2[256];
                char rxp[24] = "0";
                char rxe[24] = "0";
                char txp[24] = "0";
                char txe[24] = "0";

                trim_copy(fields[0], name, sizeof(name));
                trim_copy(fields[1], mtu, sizeof(mtu));
                trim_copy(fields[2], flags, sizeof(flags));
                trim_copy(fields[3], mac, sizeof(mac));

                snprintf(l1, sizeof(l1), "%-9s %-6s %-20s %s", name, mtu, flags, mac);
                debug_writeln(l1);
                if (parse_netdev_stats(netdev, name, rxp, sizeof(rxp), rxe, sizeof(rxe), txp, sizeof(txp), txe, sizeof(txe)) == 0)
                {
                    snprintf(l2, sizeof(l2), "  rx %s (err %s)   tx %s (err %s)", rxp, rxe, txp, txe);
                    debug_writeln(l2);
                }
                printed++;
            }
        }

        if (!line_end)
            break;
        p = line_end + 1;
    }

    if (printed == 0)
        debug_writeln("ifconfig: no network interfaces available");
}

static int cmd_ifconfig_handler(int argc, char **argv)
{
    char netinfo[768];
    char netdev[1024];
    int ret_netinfo;
    int ret_netdev;

    if (argc > 1)
    {
        debug_writeln_err("ifconfig: this implementation does not take arguments");
        debug_writeln_err("usage: ifconfig");
        return 1;
    }
    (void)argv;

    ret_netinfo = read_text_file("/proc/netinfo", netinfo, sizeof(netinfo));
    if (ret_netinfo < 0)
    {
        debug_writeln_err("ifconfig: networking unavailable (missing /proc/netinfo)");
        debug_writeln_err("hint: enable CONFIG_ENABLE_NETWORKING and ensure a net driver is initialized");
        debug_perror("ifconfig", "/proc/netinfo", ret_netinfo);
        return 1;
    }
    if (ret_netinfo == 0 || netinfo[0] == '\0')
    {
        debug_writeln_err("ifconfig: no interface metadata in /proc/netinfo");
        debug_writeln_err("hint: networking may be disabled or no interface is registered");
        return 1;
    }

    ret_netdev = read_text_file("/proc/net/dev", netdev, sizeof(netdev));
    if (ret_netdev < 0)
    {
        debug_writeln_err("ifconfig: networking stats unavailable (missing /proc/net/dev)");
        debug_writeln_err("hint: verify /proc/net/dev support in procfs and active network stack");
        debug_perror("ifconfig", "/proc/net/dev", ret_netdev);
        return 1;
    }
    if (ret_netdev == 0 || netdev[0] == '\0')
    {
        debug_writeln_err("ifconfig: no interface statistics in /proc/net/dev");
        debug_writeln_err("hint: no active network device is currently exporting counters");
        return 1;
    }

    (void)ret_netinfo;
    (void)ret_netdev;
    print_ifconfig_pretty(netinfo, netdev);

    return 0;
}

struct debug_command cmd_ifconfig = {
    .name = "ifconfig",
    .handler = cmd_ifconfig_handler,
    .usage = "ifconfig",
    .description = "Show network interface information from /proc",
    .flags = NULL
};

