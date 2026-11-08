/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: devfs_net.c
 * Description: /dev/net virtual interface access and text snapshots.
 */

#include "devfs_internal.h"

#if CONFIG_ENABLE_NETWORKING

typedef struct dev_net_config_ptrs {
    ip4_addr_t *ip;
    ip4_addr_t *netmask;
    ip4_addr_t *gateway;
} dev_net_config_ptrs_t;

typedef struct dev_net_config_values {
    ip4_addr_t ip;
    ip4_addr_t netmask;
    ip4_addr_t gateway;
} dev_net_config_values_t;

int dev_net_pid_has_ready_ping(pid_t pid)
{
    uint16_t id;

    if (pid <= 0)
        return 0;
    id = (uint16_t)(pid & 0xFFFF);
    return icmp_has_ready_echo_result(id) ? 1 : 0;
}

static int64_t dev_net_send_ping(ip4_addr_t dest_ip)
{
    struct net_device *dev = net_get_devices();
    pid_t pid;
    uint16_t id;
    uint16_t seq;

    if (!dev)
        return -ENODEV;
    pid = ir0_current_pid();
    id = (uint16_t)(pid & 0xFFFF);
    seq = icmp_allocate_echo_seq();
    return icmp_send_echo_request(dev, dest_ip, id, seq, NULL, 0) == 0
        ? 0 : -EIO;
}

static void dev_net_get_config(dev_net_config_values_t *config)
{
    config->ip = ip_local_addr;
    config->netmask = ip_netmask;
    config->gateway = ip_gateway;
}

static void dev_net_apply_config(const dev_net_config_values_t *config)
{
    struct net_device *dev;

    ip_local_addr = config->ip;
    ip_netmask = config->netmask;
    ip_gateway = config->gateway;
    arp_set_my_ip(config->ip);
    dev = net_get_devices();
    while (dev)
    {
        arp_set_interface_ip(dev, config->ip);
        dev = dev->next;
    }
    (void)ip_routes_seed_from_globals();
}
#endif /* CONFIG_ENABLE_NETWORKING */

int64_t dev_net_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
#if CONFIG_ENABLE_NETWORKING
    (void)entry; (void)offset;
    if (!buf || count == 0)
        return -EINVAL;

    /*
     * write(2) buffers are not guaranteed to be null-terminated.
     * Always copy and terminate before parsing as command text.
     */
    char cmd_local[256];
    size_t copy_len = (count < sizeof(cmd_local) - 1) ? count : (sizeof(cmd_local) - 1);
    memcpy(cmd_local, buf, copy_len);
    cmd_local[copy_len] = '\0';
    const char *cmd = cmd_local;

    /* Parse network commands (ping, ifconfig, etc.) */
    if (strncmp(cmd, "ping ", 5) == 0)
    {
        /* Parse IP address or hostname from command */
        const char *host_str = cmd + 5;
        while (*host_str == ' ' || *host_str == '\t')
            host_str++;

        /* Extract hostname/IP string (terminate at whitespace) */
        char hostname[256];
        size_t hostname_len = 0;
        const char *p = host_str;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r' && hostname_len < sizeof(hostname) - 1)
        {
            hostname[hostname_len++] = *p++;
        }
        hostname[hostname_len] = '\0';

        /* Try to parse as IP address first */
        ip4_addr_t dest_ip = 0;
        {
            uint8_t octets[4] = {0, 0, 0, 0};
            int octet_idx = 0;
            int value = 0;
            p = hostname;

            while (*p && octet_idx < 4)
            {
                if (*p >= '0' && *p <= '9')
                {
                    value = value * 10 + (*p - '0');
                    if (value > 255)
                        break;
                }
                else if (*p == '.')
                {
                    if (octet_idx >= 4)
                        break;
                    octets[octet_idx++] = (uint8_t)value;
                    value = 0;
                }
                else
                {
                    /* Not an IP, might be a hostname */
                    dest_ip = 0;
                    break;
                }
                p++;
            }

            if (octet_idx == 3 && value <= 255 && *p == '\0')
            {
                octets[octet_idx] = (uint8_t)value;
                dest_ip = htonl((octets[0] << 24) | (octets[1] << 16) | (octets[2] << 8) | octets[3]);
            }
        }

        /* If not an IP address, try DNS resolution */
        if (dest_ip == 0)
        {

            /*
             * Prefer runtime DNS from stack configuration (DHCP can update it).
             * Keep protocol-specific fallbacks for static configurations.
             */
            ip4_addr_t dns_server = dns_get_default_server();
            if (dns_server == 0)
            {
#ifdef IR0_TAP_NETWORKING
                dns_server = htonl((8U << 24) | (8U << 16) | (8U << 8) | 8U); /* 8.8.8.8 */
#else
                dns_server = htonl((10U << 24) | (0U << 16) | (2U << 8) | 3U); /* 10.0.2.3 */
#endif
            }
            {
                uint32_t dns_h = ntohl(dns_server);
                LOG_INFO_FMT("DEVNET", "Attempting DNS resolution for '%s' using DNS server %d.%d.%d.%d",
                             hostname,
                             (int)((dns_h >> 24) & 0xFF),
                             (int)((dns_h >> 16) & 0xFF),
                             (int)((dns_h >> 8) & 0xFF),
                             (int)(dns_h & 0xFF));
            }

            /* In QEMU user-mode, try gateway first (10.0.2.2) as it might forward DNS */
            if (ip_gateway != 0)
            {
                LOG_INFO_FMT("DEVNET", "Trying DNS via gateway first");
                dest_ip = dns_resolve(hostname, ip_gateway);
                if (dest_ip == 0)
                {
                    LOG_INFO_FMT("DEVNET", "Gateway DNS failed, trying direct DNS server");
                    dest_ip = dns_resolve(hostname, dns_server);
                }
                else
                {
                    LOG_INFO_FMT("DEVNET", "DNS resolution via gateway successful");
                }
            }
            else
            {
                LOG_INFO_FMT("DEVNET", "No gateway, using direct DNS server");
                dest_ip = dns_resolve(hostname, dns_server);
            }

            if (dest_ip == 0)
            {
                /* DNS resolution failed */
                LOG_INFO_FMT("DEVNET", "DNS resolution failed for '%s'", hostname);
                return -EIO;
            }
            else
            {
                LOG_INFO_FMT("DEVNET", "DNS resolution successful: '%s' -> resolved IP", hostname);
            }
        }

        /* Send ping via ioctl */
        {
            int64_t rc = dev_net_send_ping(dest_ip);
            return (rc < 0) ? rc : (int64_t)count;
        }
    }
    else if (strncmp(cmd, "dhcp", 4) == 0 &&
             (cmd[4] == '\0' || cmd[4] == '\n' || cmd[4] == '\r' ||
              cmd[4] == ' ' || cmd[4] == '\t'))
    {
        /*
         * Trigger DHCP only on explicit request from userspace/debug shell.
         * Boot keeps static network defaults unless this command is invoked.
         */
        int ret = net_stack_post_irq_init();
        if (ret < 0)
            return ret;
        return (int64_t)count;
    }
    else if (strncmp(cmd, "ifconfig", 8) == 0)
    {
        /* Parse ifconfig command: "ifconfig <ip> [netmask] [gateway]" */
        const char *config_str = cmd + 8;  /* Skip "ifconfig" */
        while (*config_str == ' ' || *config_str == '\t')
            config_str++;

        if (*config_str == '\0' || *config_str == '\n')
        {
            /* No arguments: show current config via ioctl */
            dev_net_config_values_t config;

            dev_net_get_config(&config);
            {
                /* Format and display configuration */
                char buf[256];

                /* Format IP addresses */
                uint32_t ip_h = ntohl(config.ip);
                uint32_t netmask_h = ntohl(config.netmask);
                uint32_t gateway_h = ntohl(config.gateway);

                snprintf(buf, sizeof(buf),
                        "IP: %d.%d.%d.%d\n"
                        "Netmask: %d.%d.%d.%d\n"
                        "Gateway: %d.%d.%d.%d\n",
                        (int)((ip_h >> 24) & 0xFF), (int)((ip_h >> 16) & 0xFF),
                        (int)((ip_h >> 8) & 0xFF), (int)(ip_h & 0xFF),
                        (int)((netmask_h >> 24) & 0xFF), (int)((netmask_h >> 16) & 0xFF),
                        (int)((netmask_h >> 8) & 0xFF), (int)(netmask_h & 0xFF),
                        (int)((gateway_h >> 24) & 0xFF), (int)((gateway_h >> 16) & 0xFF),
                        (int)((gateway_h >> 8) & 0xFF), (int)(gateway_h & 0xFF));

                /* Write to stdout */
                console_backend_write(buf, strlen(buf), 0x0F);
            }
        }
        else
        {
            /* Parse IP, netmask, gateway */
            char config_copy[256];
            size_t i = 0;
            const char *p = config_str;
            while (i < sizeof(config_copy) - 1 && *p && *p != '\n' && *p != '\r')
                config_copy[i++] = *p++;
            config_copy[i] = '\0';

            /* Parse IP address */
            char *ip_str = config_copy;
            char *netmask_str = NULL;
            char *gateway_str = NULL;

            /* Find netmask */
            char *q = ip_str;
            while (*q && *q != ' ' && *q != '\t')
                q++;
            if (*q)
            {
                *q++ = '\0';
                netmask_str = q;
                while (*netmask_str == ' ' || *netmask_str == '\t')
                    netmask_str++;

                /* Find gateway */
                q = netmask_str;
                while (*q && *q != ' ' && *q != '\t')
                    q++;
                if (*q)
                {
                    *q++ = '\0';
                    gateway_str = q;
                    while (*gateway_str == ' ' || *gateway_str == '\t')
                        gateway_str++;
                    if (*gateway_str == '\0')
                        gateway_str = NULL;
                }

                /* Check if netmask is empty */
                if (netmask_str[0] == '\0')
                    netmask_str = NULL;
            }

            /* Parse IP addresses */
            uint8_t ip_octets[4] = {0};
            uint8_t netmask_octets[4] = {0};
            uint8_t gateway_octets[4] = {0};

            /* Parse IP */
            int octet_idx = 0;
            int value = 0;
            const char *parse_ptr = ip_str;
            while (*parse_ptr && octet_idx < 4)
            {
                if (*parse_ptr >= '0' && *parse_ptr <= '9')
                {
                    value = value * 10 + (*parse_ptr - '0');
                    if (value > 255)
                        return -EINVAL;
                }
                else if (*parse_ptr == '.')
                {
                    ip_octets[octet_idx++] = (uint8_t)value;
                    value = 0;
                }
                else
                    return -EINVAL;
                parse_ptr++;
            }
            if (octet_idx == 3)
                ip_octets[octet_idx] = (uint8_t)value;
            else
                return -EINVAL;

            ip4_addr_t new_ip = htonl((ip_octets[0] << 24) | (ip_octets[1] << 16) |
                                      (ip_octets[2] << 8) | ip_octets[3]);
            ip4_addr_t new_netmask = 0;
            ip4_addr_t new_gateway = 0;

            /* Parse netmask if provided */
            if (netmask_str)
            {
                octet_idx = 0;
                value = 0;
                parse_ptr = netmask_str;
                while (*parse_ptr && octet_idx < 4)
                {
                    if (*parse_ptr >= '0' && *parse_ptr <= '9')
                    {
                        value = value * 10 + (*parse_ptr - '0');
                        if (value > 255)
                            return -EINVAL;
                    }
                    else if (*parse_ptr == '.')
                    {
                        netmask_octets[octet_idx++] = (uint8_t)value;
                        value = 0;
                    }
                    else
                        return -EINVAL;
                    parse_ptr++;
                }
                if (octet_idx == 3)
                    netmask_octets[octet_idx] = (uint8_t)value;
                else
                    return -EINVAL;

                new_netmask = htonl((netmask_octets[0] << 24) | (netmask_octets[1] << 16) |
                                    (netmask_octets[2] << 8) | netmask_octets[3]);
            }

            /* Parse gateway if provided */
            if (gateway_str)
            {
                octet_idx = 0;
                value = 0;
                parse_ptr = gateway_str;
                while (*parse_ptr && octet_idx < 4)
                {
                    if (*parse_ptr >= '0' && *parse_ptr <= '9')
                    {
                        value = value * 10 + (*parse_ptr - '0');
                        if (value > 255)
                            return -EINVAL;
                    }
                    else if (*parse_ptr == '.')
                    {
                        gateway_octets[octet_idx++] = (uint8_t)value;
                        value = 0;
                    }
                    else
                        return -EINVAL;
                    parse_ptr++;
                }
                if (octet_idx == 3)
                    gateway_octets[octet_idx] = (uint8_t)value;
                else
                    return -EINVAL;

                new_gateway = htonl((gateway_octets[0] << 24) | (gateway_octets[1] << 16) |
                                    (gateway_octets[2] << 8) | gateway_octets[3]);
            }

            /* Set configuration via ioctl */
            dev_net_config_values_t config = {
                .ip = new_ip,
                .netmask = new_netmask,
                .gateway = new_gateway
            };

            if (!ir0_cred_is_root())
                return -EPERM;
            dev_net_apply_config(&config);
            return (int64_t)count;
        }
        return (int64_t)count;
    }

    return -EINVAL;
#else
    (void)entry; (void)buf; (void)count; (void)offset;
    return -ENODEV;
#endif
}

#if CONFIG_ENABLE_NETWORKING
/*
 * Build one /dev/net text payload into @out (ping_result or snapshot).
 * Returns byte length (not including a forced NUL).
 */
static size_t dev_net_build_text(char *out, size_t out_len)
{
	size_t off = 0;
	int n;
	pid_t pid;
	uint16_t id;
	uint16_t seq = 0;
	uint64_t rtt = 0;
	uint8_t ttl = 0;
	size_t payload_bytes = 0;
	ip4_addr_t reply_ip = 0;
	struct net_device *dev;

	if (!out || out_len == 0)
		return 0;

	net_poll();
	pid = ir0_current_pid();
	id = (uint16_t)(pid & 0xFFFF);

	if (icmp_get_next_echo_result(id, &seq, &rtt, &ttl, &payload_bytes, &reply_ip))
	{
		uint32_t ip = ntohl(reply_ip);

		n = snprintf(out, out_len,
			     "type=ping_result\nsuccess=1\nseq=%u\nrtt_ms=%u\nttl=%u\npayload_bytes=%u\nip=%u.%u.%u.%u\n",
			     (unsigned)seq, (unsigned)rtt, (unsigned)ttl,
			     (unsigned)payload_bytes,
			     (unsigned)((ip >> 24) & 0xFF), (unsigned)((ip >> 16) & 0xFF),
			     (unsigned)((ip >> 8) & 0xFF), (unsigned)(ip & 0xFF));
		if (n <= 0)
			return 0;
		if ((size_t)n >= out_len)
			return out_len - 1;
		return (size_t)n;
	}

	n = snprintf(out + off, out_len - off, "type=snapshot\n");
	if (n > 0 && (size_t)n < out_len - off)
		off += (size_t)n;
	n = snprintf(out + off, out_len - off, "iface\tmtu\tflags\tmac\n");
	if (n > 0 && (size_t)n < out_len - off)
		off += (size_t)n;

	dev = net_get_devices();
	while (dev && off < out_len - 1)
	{
		char flags[32];
		size_t foff = 0;

		flags[0] = '\0';
		if (dev->flags & IFF_UP)
			foff += (size_t)snprintf(flags + foff, sizeof(flags) - foff, "UP");
		if (dev->flags & IFF_RUNNING)
			foff += (size_t)snprintf(flags + foff, sizeof(flags) - foff,
						 "%sRUNNING", foff ? "," : "");
		if (dev->flags & IFF_BROADCAST)
			foff += (size_t)snprintf(flags + foff, sizeof(flags) - foff,
						 "%sBROADCAST", foff ? "," : "");
		if (foff == 0)
			snprintf(flags, sizeof(flags), "-");

		n = snprintf(out + off, out_len - off,
			     "%s\t%u\t%s\t%02x:%02x:%02x:%02x:%02x:%02x\n",
			     dev->name ? dev->name : "", (unsigned)dev->mtu, flags,
			     (unsigned)dev->mac[0], (unsigned)dev->mac[1],
			     (unsigned)dev->mac[2], (unsigned)dev->mac[3],
			     (unsigned)dev->mac[4], (unsigned)dev->mac[5]);
		if (n <= 0 || (size_t)n >= out_len - off)
			break;
		off += (size_t)n;
		dev = dev->next;
	}

	if (off < out_len)
	{
		uint32_t ip_h = ntohl(ip_local_addr);
		uint32_t nm_h = ntohl(ip_netmask);
		uint32_t gw_h = ntohl(ip_gateway);

		n = snprintf(out + off, out_len - off,
			     "ip=%u.%u.%u.%u\nnetmask=%u.%u.%u.%u\ngateway=%u.%u.%u.%u\n",
			     (unsigned)((ip_h >> 24) & 0xFF), (unsigned)((ip_h >> 16) & 0xFF),
			     (unsigned)((ip_h >> 8) & 0xFF), (unsigned)(ip_h & 0xFF),
			     (unsigned)((nm_h >> 24) & 0xFF), (unsigned)((nm_h >> 16) & 0xFF),
			     (unsigned)((nm_h >> 8) & 0xFF), (unsigned)(nm_h & 0xFF),
			     (unsigned)((gw_h >> 24) & 0xFF), (unsigned)((gw_h >> 16) & 0xFF),
			     (unsigned)((gw_h >> 8) & 0xFF), (unsigned)(gw_h & 0xFF));
		if (n > 0 && (size_t)n < out_len - off)
			off += (size_t)n;
	}

	return off;
}
#endif /* CONFIG_ENABLE_NETWORKING */

int devfs_node_wants_text_snap(uint32_t device_id)
{
	return device_id == DEVFS_ID_NET || device_id == DEVFS_ID_KMSG;
}

int64_t devfs_text_snap_read(const devfs_text_snap_t *snap, void *buf,
			     size_t count, off_t offset)
{
	size_t avail;
	size_t to_copy;

	if (!snap || !snap->buf)
		return -EINVAL;
	if (!buf)
		return -EFAULT;
	if (offset < 0)
		return -EINVAL;
	if ((uint64_t)offset >= (uint64_t)snap->len)
		return 0;
	avail = snap->len - (size_t)offset;
	to_copy = avail < count ? avail : count;
	memcpy(buf, snap->buf + (size_t)offset, to_copy);
	return (int64_t)to_copy;
}

void devfs_text_snap_acquire(devfs_text_snap_t *snap)
{
	unsigned long irq_flags;

	if (!snap)
		return;

	irq_flags = irq_save();
	snap->refs++;
	irq_restore(irq_flags);
}

void devfs_text_snap_release(devfs_text_snap_t *snap)
{
	unsigned long irq_flags;
	int last;

	if (!snap)
		return;

	irq_flags = irq_save();
	if (snap->refs > 0)
		snap->refs--;
	last = (snap->refs == 0);
	irq_restore(irq_flags);

	if (!last)
		return;
	if (snap->buf)
		kfree(snap->buf);
	kfree(snap);
}

devfs_text_snap_t *devfs_text_snap_capture(uint32_t device_id)
{
	devfs_text_snap_t *snap;
	char tmp[2048];
	size_t len = 0;

	if (device_id == DEVFS_ID_KMSG)
	{
		int n = klog_read_records(tmp, sizeof(tmp));

		if (n < 0)
			return NULL;
		len = (size_t)n;
	}
#if CONFIG_ENABLE_NETWORKING
	else if (device_id == DEVFS_ID_NET)
		len = dev_net_build_text(tmp, sizeof(tmp));
#endif
	else
		return NULL;

	snap = (devfs_text_snap_t *)kmalloc(sizeof(*snap));
	if (!snap)
		return NULL;
	snap->buf = (char *)kmalloc(len + 1);
	if (!snap->buf)
	{
		kfree(snap);
		return NULL;
	}
	memcpy(snap->buf, tmp, len);
	snap->buf[len] = '\0';
	snap->len = len;
	snap->refs = 1;
	return snap;
}

int64_t dev_net_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
#if CONFIG_ENABLE_NETWORKING
	char full[2048];
	size_t len;
	size_t avail;
	size_t to_copy;

	(void)entry;
	if (!buf)
		return -EFAULT;
	if (count == 0)
		return 0;
	if (offset < 0)
		return -EINVAL;

	/*
	 * Bounce+slice fallback when no per-open snap is attached (ops path).
	 * Preferred: open captures snap; sys_read uses devfs_text_snap_read.
	 */
	len = dev_net_build_text(full, sizeof(full));
	if ((uint64_t)offset >= (uint64_t)len)
		return 0;
	avail = len - (size_t)offset;
	to_copy = avail < count ? avail : count;
	memcpy(buf, full + (size_t)offset, to_copy);
	return (int64_t)to_copy;
#else
	(void)entry;
	(void)buf;
	(void)count;
	(void)offset;
	return -ENODEV;
#endif
}

int64_t dev_net_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
#if CONFIG_ENABLE_NETWORKING
    (void)entry;

    /* Network device may not be available, return -1 if needed */

    switch (request)
    {
        case NET_SEND_PING:
            if (arg)
            {
                ip4_addr_t dest_ip;

                if (copy_from_user(&dest_ip, arg, sizeof(dest_ip)) != 0)
                    return -EFAULT;
                return dev_net_send_ping(dest_ip);
            }
            return -EINVAL;

        case NET_GET_CONFIG:
            if (arg)
            {
                dev_net_config_ptrs_t user_ptrs;
                dev_net_config_values_t values;

                if (copy_from_user(&user_ptrs, arg, sizeof(user_ptrs)) != 0)
                    return -EFAULT;
                dev_net_get_config(&values);
                if (user_ptrs.ip && copy_to_user(user_ptrs.ip, &values.ip,
                                                 sizeof(values.ip)) != 0)
                    return -EFAULT;
                if (user_ptrs.netmask &&
                    copy_to_user(user_ptrs.netmask, &values.netmask,
                                 sizeof(values.netmask)) != 0)
                    return -EFAULT;
                if (user_ptrs.gateway &&
                    copy_to_user(user_ptrs.gateway, &values.gateway,
                                 sizeof(values.gateway)) != 0)
                    return -EFAULT;
                return 0;
            }
            return -EINVAL;

        case NET_SET_CONFIG:
            if (arg)
            {
                dev_net_config_values_t config;

                if (!ir0_cred_is_root())
                    return -EPERM;
                if (copy_from_user(&config, arg, sizeof(config)) != 0)
                    return -EFAULT;
                dev_net_apply_config(&config);
                return 0;
            }
            return -EINVAL;

        case NET_GET_PING_RESULT:
            if (arg)
            {
                /* arg points to: { int success; uint64_t rtt; uint8_t ttl; size_t payload_bytes; ip4_addr_t reply_ip; } */
                struct ping_result result = {0};

                /* Get PID to use as ICMP ID (matches NET_SEND_PING behavior) */
                pid_t pid = ir0_current_pid();
                uint16_t id = (uint16_t)(pid & 0xFFFF);

                /* Try to get next completed echo result for this pid. */
                if (icmp_get_next_echo_result(id, &result.seq, &result.rtt, &result.ttl,
                                              &result.payload_bytes, &result.reply_ip))
                {
                    result.success = 1;
                }
                else
                {
                    result.success = 0;
                    result.seq = 0;
                }
                return copy_to_user(arg, &result, sizeof(result)) == 0
                    ? 0 : -EFAULT;
            }
            return -EINVAL;

        default:
            return -ENOTTY;  /* Invalid request */
    }
#else
    (void)entry; (void)request; (void)arg;
    return -ENODEV;
#endif
}
