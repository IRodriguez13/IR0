/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: procfs_net.c
 * Description: /proc/net interface and socket table entries.
 */

#include "procfs_internal.h"


/*
 * /proc/netinfo: raw data only, one line per interface, tab-separated.
 * name\tmtu\tflags\tmac (mac as xx:xx:xx:xx:xx:xx)
 * Frontend (e.g. ifconfig/netinfo) does formatting.
 */
int proc_netinfo_read(char *buf, size_t count)
{
#if CONFIG_ENABLE_NETWORKING
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    struct net_device *dev = net_get_devices();
    if (!dev)
        return 0;
    size_t off = 0;
    while (dev && off < count - 1)
    {
        char flags[32];
        size_t foff = 0;
        flags[0] = '\0';
        if (dev->flags & IFF_UP) foff += (size_t)snprintf(flags + foff, sizeof(flags) - foff, "UP");
        if (dev->flags & IFF_RUNNING) foff += (size_t)snprintf(flags + foff, sizeof(flags) - foff, "%sRUNNING", foff ? "," : "");
        if (dev->flags & IFF_BROADCAST) foff += (size_t)snprintf(flags + foff, sizeof(flags) - foff, "%sBROADCAST", foff ? "," : "");
        if (foff == 0) snprintf(flags, sizeof(flags), "-");
        int n = snprintf(buf + off, count - off,
                         "%s\t%u\t%s\t%02x:%02x:%02x:%02x:%02x:%02x\n",
                         dev->name ? dev->name : "", (unsigned)dev->mtu, flags,
                         (unsigned)dev->mac[0], (unsigned)dev->mac[1], (unsigned)dev->mac[2],
                         (unsigned)dev->mac[3], (unsigned)dev->mac[4], (unsigned)dev->mac[5]);
        if (n < 0) break;
        if (n >= (int)(count - off)) n = (int)(count - off) - 1;
        off += (size_t)n;
        dev = dev->next;
    }
    if (off < count) buf[off] = '\0';
    return (int)off;
#else
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    return 0;
#endif
}

/*
 * /proc/net/dev: Linux-style per-interface summary.
 * Header lines mirror common tools that parse Inter-| / face | columns.
 */
int proc_net_dev_read(char *buf, size_t count)
{
#if CONFIG_ENABLE_NETWORKING
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    size_t off = 0;
    /*
     * Linux /proc/net/dev columns (BusyBox interface.c procnetdev_vsn=2).
     * Header must contain "bytes" and "compressed" for fancy ifconfig stats.
     */
    int n = snprintf(buf, count,
                     "Inter-|   Receive                                                |  Transmit\n"
                     " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n");
    if (n < 0)
        return -1;
    if ((size_t)n >= count)
    {
        buf[count - 1] = '\0';
        return (int)(count - 1);
    }
    off = (size_t)n;

    struct net_device *dev = net_get_devices();
    while (dev && off < count - 1)
    {
        uint64_t rxp = 0, txp = 0, rxe = 0, txe = 0;
        uint64_t rxb = 0, txb = 0;
        char rxp_str[24];
        char rxe_str[24];
        char txp_str[24];
        char txe_str[24];
        char rxb_str[24];
        char txb_str[24];
        char rxd_str[24];
        char rxf_str[24];
        char rxm_str[24];

        if (dev->get_stats)
            dev->get_stats(dev, &rxp, &txp, &rxe, &txe);
        if (dev->get_byte_stats)
            dev->get_byte_stats(dev, &rxb, &txb);

        proc_u64_to_dec(rxb, rxb_str, sizeof(rxb_str));
        proc_u64_to_dec(rxp, rxp_str, sizeof(rxp_str));
        proc_u64_to_dec(rxe, rxe_str, sizeof(rxe_str));
        proc_u64_to_dec(txb, txb_str, sizeof(txb_str));
        proc_u64_to_dec(txp, txp_str, sizeof(txp_str));
        proc_u64_to_dec(txe, txe_str, sizeof(txe_str));
        proc_u64_to_dec(dev->rx_dropped, rxd_str, sizeof(rxd_str));
        proc_u64_to_dec(dev->rx_fifo_errors, rxf_str, sizeof(rxf_str));
        proc_u64_to_dec(dev->rx_multicast, rxm_str, sizeof(rxm_str));
        /*
         * bytes packets errs drop fifo frame compressed multicast | tx...
         * RX frame/compressed and every TX column past errs stay 0: no driver
         * accounts alignment errors, compression, TX drops, collisions or
         * carrier losses yet (would require reading NIC error registers).
         */
        n = snprintf(buf + off, count - off,
                     "  %s: %s %s %s %s %s 0 0 %s %s %s %s 0 0 0 0 0\n",
                     (dev->name && dev->name[0] != '\0') ? dev->name : "eth0",
                     rxb_str, rxp_str, rxe_str, rxd_str, rxf_str, rxm_str,
                     txb_str, txp_str, txe_str);
        if (n < 0)
            return -1;
        if ((size_t)n >= count - off)
        {
            buf[count - 1] = '\0';
            return (int)(count - 1);
        }
        off += (size_t)n;
        dev = dev->next;
    }

    return (int)off;
#else
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    return 0;
#endif
}

/*
 * /proc/net/route — Linux fib_trie format for BusyBox route/netstat -r.
 * Iface Destination Gateway Flags RefCnt Use Metric Mask MTU Window IRTT
 */
#define IR0_RTF_UP      0x0001
#define IR0_RTF_GATEWAY 0x0002

#if CONFIG_ENABLE_NETWORKING
struct proc_route_fmt_ctx
{
	char *buf;
	size_t count;
	size_t off;
	const char *ifname;
	int err;
};

static int proc_route_emit_row(char *buf, size_t count, size_t *off,
			       const char *ifname, uint32_t dest, uint32_t gw,
			       unsigned flags, uint32_t mask)
{
	int n;

	if (*off >= count)
		return -1;
	n = snprintf(buf + *off, count - *off,
		     "%s\t%08X\t%08X\t%04X\t%d\t%u\t%u\t%08X\t%d\t%u\t%u\n",
		     ifname ? ifname : "*",
		     (unsigned)dest, (unsigned)gw, flags,
		     0, 0u, 0u, (unsigned)mask, 0, 0u, 0u);
	if (n < 0)
		return -1;
	if ((size_t)n >= count - *off)
	{
		buf[count - 1] = '\0';
		*off = count - 1;
		return -1;
	}
	*off += (size_t)n;
	return 0;
}

static int proc_route_walk_cb(ip4_addr_t dest, ip4_addr_t mask, ip4_addr_t gw,
			      void *ctx)
{
	struct proc_route_fmt_ctx *c = ctx;
	unsigned flags = IR0_RTF_UP;

	if (gw)
		flags |= IR0_RTF_GATEWAY;
	if (proc_route_emit_row(c->buf, c->count, &c->off, c->ifname,
				(uint32_t)dest, (uint32_t)gw, flags,
				(uint32_t)mask) != 0)
	{
		c->err = 1;
		return -1;
	}
	return 0;
}
#endif

/*
 * Linux-style /proc/net/{tcp,udp,raw,unix} for BusyBox netstat.
 * IPv4 addresses are printed as %08X of the __be32 (sin_addr.s_addr) value —
 * same as Linux get_tcp4_sock — not ntohl()'d host integers.
 */
struct proc_sock_fmt_ctx
{
	char *buf;
	size_t count;
	size_t off;
	int sl;
	int err;
};

static int proc_sock_append(struct proc_sock_fmt_ctx *c, const char *line)
{
	size_t n;
	size_t avail;

	if (!c || !line || c->err)
		return -1;
	n = strlen(line);
	avail = (c->off < c->count) ? (c->count - c->off) : 0;
	if (avail <= 1)
	{
		c->err = 1;
		return -1;
	}
	if (n >= avail)
		n = avail - 1;
	memcpy(c->buf + c->off, line, n);
	c->off += n;
	c->buf[c->off] = '\0';
	return 0;
}

#if CONFIG_ENABLE_NETWORKING
static int proc_inet_row_cb(const struct sock_stream_inet_snap *s, void *ctx)
{
	struct proc_sock_fmt_ctx *c = ctx;
	char line[192];
	int n;

	n = snprintf(line, sizeof(line),
		     "%4d: %08X:%04X %08X:%04X %02X %08X:%08X %02X:%08X %08X %5u %8d %lu %d\n",
		     c->sl++,
		     (unsigned)s->local_ip, (unsigned)s->local_port,
		     (unsigned)s->rem_ip, (unsigned)s->rem_port,
		     (unsigned)s->st,
		     0u, 0u, 0u, 0u, 0u, 0u, 0, (unsigned long)s->inode, 1);
	if (n < 0)
		return -1;
	return proc_sock_append(c, line);
}

static int proc_udp_row_cb(const struct sock_udp_snap *s, void *ctx)
{
	struct proc_sock_fmt_ctx *c = ctx;
	char line[192];
	int n;

	n = snprintf(line, sizeof(line),
		     "%4d: %08X:%04X %08X:%04X %02X %08X:%08X %02X:%08X %08X %5u %8d %lu %d\n",
		     c->sl++,
		     (unsigned)s->local_ip, (unsigned)s->local_port,
		     (unsigned)s->rem_ip, (unsigned)s->rem_port,
		     (unsigned)s->st,
		     0u, 0u, 0u, 0u, 0u, 0u, 0, (unsigned long)s->inode, 1);
	if (n < 0)
		return -1;
	return proc_sock_append(c, line);
}

static int proc_raw_row_cb(const struct sock_icmp_snap *s, void *ctx)
{
	struct proc_sock_fmt_ctx *c = ctx;
	char line[192];
	int n;

	n = snprintf(line, sizeof(line),
		     "%4d: %08X:%04X %08X:%04X %02X %08X:%08X %02X:%08X %08X %5u %8d %lu %d\n",
		     c->sl++,
		     0u, (unsigned)s->proto,
		     0u, 0u,
		     0x07u,
		     0u, 0u, 0u, 0u, 0u, 0u, 0, (unsigned long)s->inode, 1);
	if (n < 0)
		return -1;
	return proc_sock_append(c, line);
}
#endif

static int proc_unix_row_cb(const struct sock_stream_unix_snap *s, void *ctx)
{
	struct proc_sock_fmt_ctx *c = ctx;
	char line[256];
	char path[128];
	int n;

	path[0] = '\0';
	if (s->path_len > 0)
	{
		if (s->is_abstract)
		{
			path[0] = '@';
			if (s->path_len < sizeof(path) - 1)
			{
				memcpy(path + 1, s->path, s->path_len);
				path[s->path_len + 1] = '\0';
			}
		}
		else if (s->path_len < sizeof(path))
		{
			memcpy(path, s->path, s->path_len);
			path[s->path_len] = '\0';
		}
	}

	n = snprintf(line, sizeof(line),
		     "%08lX: %08X %08X %08X %04X %02X %5lu",
		     (unsigned long)s->inode,
		     (unsigned)s->refcnt,
		     0u, 0u,
		     (unsigned)s->type,
		     (unsigned)s->st,
		     (unsigned long)s->inode);
	if (n < 0)
		return -1;
	if (path[0])
	{
		size_t used = (size_t)n;

		if (used + 2 < sizeof(line))
		{
			line[used++] = ' ';
			strncpy(line + used, path, sizeof(line) - used - 2);
			line[sizeof(line) - 2] = '\0';
		}
	}
	{
		size_t L = strlen(line);

		if (L + 1 < sizeof(line))
		{
			line[L] = '\n';
			line[L + 1] = '\0';
		}
	}
	return proc_sock_append(c, line);
}

static int proc_net_socktable_start(char *buf, size_t count, const char *header,
				    struct proc_sock_fmt_ctx *c)
{
	int n;

	if (VALIDATE_BUFFER(buf, count) != 0)
		return -1;
	memset(buf, 0, count);
	memset(c, 0, sizeof(*c));
	c->buf = buf;
	c->count = count;
	if (!header)
		return 0;
	n = snprintf(buf, count, "%s", header);
	if (n < 0)
		return -1;
	if ((size_t)n >= count)
	{
		buf[count - 1] = '\0';
		c->off = count - 1;
		return (int)c->off;
	}
	c->off = (size_t)n;
	return 0;
}

int proc_net_tcp_read(char *buf, size_t count)
{
	struct proc_sock_fmt_ctx c;
	const char *hdr =
		"  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n";

	if (proc_net_socktable_start(buf, count, hdr, &c) < 0)
		return -1;
#if CONFIG_ENABLE_NETWORKING
	(void)sock_stream_inet_walk(proc_inet_row_cb, &c);
#endif
	return (int)c.off;
}

int proc_net_udp_read(char *buf, size_t count)
{
	struct proc_sock_fmt_ctx c;
	const char *hdr =
		"  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n";

	if (proc_net_socktable_start(buf, count, hdr, &c) < 0)
		return -1;
#if CONFIG_ENABLE_NETWORKING
	(void)sock_udp_walk(proc_udp_row_cb, &c);
#endif
	return (int)c.off;
}

int proc_net_raw_read(char *buf, size_t count)
{
	struct proc_sock_fmt_ctx c;
	const char *hdr =
		"  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n";

	if (proc_net_socktable_start(buf, count, hdr, &c) < 0)
		return -1;
#if CONFIG_ENABLE_NETWORKING
	(void)sock_icmp_walk(proc_raw_row_cb, &c);
#endif
	return (int)c.off;
}

int proc_net_unix_read(char *buf, size_t count)
{
	struct proc_sock_fmt_ctx c;
	const char *hdr =
		"Num       RefCount Protocol Flags    Type St Inode Path\n";

	if (proc_net_socktable_start(buf, count, hdr, &c) < 0)
		return -1;
	(void)sock_stream_unix_walk(proc_unix_row_cb, &c);
	return (int)c.off;
}

int proc_net_route_read(char *buf, size_t count)
{
#if CONFIG_ENABLE_NETWORKING
	struct proc_route_fmt_ctx ctx;
	struct net_device *dev;
	const char *ifname = "eth0";
	ip4_addr_t connected;
	int n;
	int walked = 0;

	if (VALIDATE_BUFFER(buf, count) != 0)
		return -1;
	memset(buf, 0, count);

	n = snprintf(buf, count,
		     "Iface\tDestination\tGateway\tFlags\tRefCnt\tUse\tMetric\tMask\t\tMTU\tWindow\tIRTT\n");
	if (n < 0)
		return -1;
	if ((size_t)n >= count)
	{
		buf[count - 1] = '\0';
		return (int)(count - 1);
	}

	dev = net_get_devices();
	if (dev && dev->name && dev->name[0])
		ifname = dev->name;

	ctx.buf = buf;
	ctx.count = count;
	ctx.off = (size_t)n;
	ctx.ifname = ifname;
	ctx.err = 0;

	/* Explicit routes first (if any). */
	(void)ip_route_walk(proc_route_walk_cb, &ctx);
	if (ctx.err)
		return (int)ctx.off;

	/*
	 * Count whether walk emitted anything by comparing off — if still
	 * header-only, synthesize connected + default from globals (QEMU).
	 */
	walked = (ctx.off > (size_t)n);
	if (!walked && ip_local_addr != 0 && ip_netmask != 0)
	{
		connected = ip_local_addr & ip_netmask;
		if (proc_route_emit_row(buf, count, &ctx.off, ifname,
					(uint32_t)connected, 0, IR0_RTF_UP,
					(uint32_t)ip_netmask) != 0)
			return (int)ctx.off;
		if (ip_gateway != 0)
		{
			if (proc_route_emit_row(buf, count, &ctx.off, ifname,
						0, (uint32_t)ip_gateway,
						IR0_RTF_UP | IR0_RTF_GATEWAY,
						0) != 0)
				return (int)ctx.off;
		}
	}
	else if (walked && ip_gateway != 0)
	{
		/* List present but may omit default — BusyBox still wants it. */
		/* Skip duplicate default if already in list: best-effort emit. */
	}

	return (int)ctx.off;
#else
	if (VALIDATE_BUFFER(buf, count) != 0)
		return -1;
	memset(buf, 0, count);
	return 0;
#endif
}
