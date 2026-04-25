/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: cmd_ping.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: ping
 * Copyright (C) 2025 Iván Rodriguez
 *
 * Ping command - Send ICMP Echo Request (uses only syscalls)
 */

#include "debug_bins.h"
#include <ir0/fcntl.h>
#include <ir0/net.h>
#include <ir0/poll.h>
#include <ir0/syscall.h>
#include <string.h>
#include <stdbool.h>

/*
 * Private ioctl for /dev/net: copy the last completed ping result into the
 * caller's buffer (struct ping_result). Must match NET_GET_PING_RESULT in
 * includes/ir0/devfs.h; duplicated here because debug_bins avoid kernel-only
 * headers and only use syscalls.
 */
#define NET_GET_PING_RESULT  0x3004

static int parse_positive_int(const char *s)
{
    int value = 0;

    if (!s || *s == '\0')
        return -EINVAL;

    while (*s)
    {
        if (*s < '0' || *s > '9')
            return -EINVAL;
        value = value * 10 + (*s - '0');
        if (value <= 0)
            return -EINVAL;
        s++;
    }

    return value;
}

static int cmd_ping_handler(int argc, char **argv)
{
    const char *target = NULL;
    int count = 1;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-c") == 0)
        {
            if (i + 1 >= argc)
            {
                debug_write_err("ping: missing value for -c\n");
                debug_write_err("Usage: ping [-c COUNT] <IP_ADDRESS_OR_HOSTNAME>\n");
                debug_serial_fail("ping", "usage");
                return 1;
            }
            count = parse_positive_int(argv[++i]);
            if (count < 1)
            {
                debug_write_err("ping: invalid count, expected positive integer\n");
                debug_serial_fail("ping", "usage");
                return 1;
            }
            continue;
        }
        if (argv[i][0] == '-')
        {
            debug_write_err("ping: unknown option\n");
            debug_write_err("Usage: ping [-c COUNT] <IP_ADDRESS_OR_HOSTNAME>\n");
            debug_serial_fail("ping", "usage");
            return 1;
        }
        if (!target)
        {
            target = argv[i];
            continue;
        }
        debug_write_err("ping: too many arguments\n");
        debug_write_err("Usage: ping [-c COUNT] <IP_ADDRESS_OR_HOSTNAME>\n");
        debug_serial_fail("ping", "usage");
        return 1;
    }

    if (!target)
    {
        debug_write_err("Usage: ping [-c COUNT] <IP_ADDRESS_OR_HOSTNAME>\n");
        debug_write_err("Example: ping -c 4 10.0.2.2\n");
        debug_write_err("Example: ping www.google.com\n");
        debug_serial_fail("ping", "usage");
        return 1;
    }

    /* Display ping header in Linux format */
    char header[256];
    int header_len = snprintf(header, sizeof(header), "PING %s (%d packets)\n", target, count);
    if (header_len > 0 && header_len < (int)sizeof(header))
    {
        debug_write(header);
    }

    /* Open /dev/net for ioctl operations */
    int fd = syscall(SYS_OPEN, (uint64_t)"/dev/net", O_RDONLY, 0);
    if (fd < 0)
    {
        debug_write_err("ping: failed to open /dev/net\n");
        debug_serial_fail("ping", "open");
        return 1;
    }

    int sent = 0;
    int received = 0;
    for (int seq = 0; seq < count; seq++)
    {
        struct ping_result result;
        bool got_response = false;
        int timeout_attempts = 50;
        struct pollfd pfd = { .fd = fd, .events = POLLIN, .revents = 0 };

        /* Send ping via /dev/net */
        int64_t ret = ir0_ping(target);
        if (ret < 0)
        {
            debug_write_err("ping: failed to send ICMP echo request\n");
            debug_write_err("Note: If using hostname, ensure DNS is configured\n");
            syscall(SYS_CLOSE, fd, 0, 0);
            debug_serial_fail("ping", "send");
            return 1;
        }
        sent++;

        for (int attempt = 0; attempt < timeout_attempts && !got_response; attempt++)
        {
            int64_t poll_ret = syscall(SYS_POLL, (uint64_t)&pfd, 1, 100);
            if (poll_ret < 0)
                break;

            /* Check for ping result via ioctl */
            int64_t ioctl_ret = syscall(SYS_IOCTL, fd, NET_GET_PING_RESULT, (uint64_t)&result);
            if (ioctl_ret == 0 && result.success == 1)
            {
                got_response = true;
                received++;

                /* Parse IP address from network byte order */
                uint32_t ip_net = result.reply_ip;
                uint32_t ip_host = ntohl(ip_net);
                uint8_t ip[4] = {
                    (uint8_t)((ip_host >> 24) & 0xFF),
                    (uint8_t)((ip_host >> 16) & 0xFF),
                    (uint8_t)((ip_host >> 8) & 0xFF),
                    (uint8_t)(ip_host & 0xFF)
                };

                /* Display in Linux format */
                char output[256];
                char rtt_str[32];

                debug_u64_to_dec((uint64_t)result.rtt, rtt_str, sizeof(rtt_str));

                int output_len = snprintf(output, sizeof(output),
                    "%d bytes from %d.%d.%d.%d: icmp_seq=%d ttl=%d time=%s ms\n",
                    (int)result.payload_bytes, ip[0], ip[1], ip[2], ip[3], (int)result.seq, (int)result.ttl, rtt_str);

                if (output_len > 0 && output_len < (int)sizeof(output))
                {
                    debug_write(output);
                }
            }
        }

        if (!got_response)
        {
            char timeout_line[128];
            int tlen = snprintf(timeout_line, sizeof(timeout_line),
                                "Request timeout for icmp_seq %d\n", seq);
            if (tlen > 0 && tlen < (int)sizeof(timeout_line))
                debug_write_err(timeout_line);
        }
    }

    syscall(SYS_CLOSE, fd, 0, 0);

    char summary[128];
    int loss = (sent > 0) ? ((sent - received) * 100) / sent : 0;
    int slen = snprintf(summary, sizeof(summary),
                        "--- %s ping statistics ---\n%d packets transmitted, %d received, %d%% packet loss\n",
                        target, sent, received, loss);
    if (slen > 0 && slen < (int)sizeof(summary))
        debug_write(summary);

    if (received == 0)
    {
        debug_serial_fail("ping", "timeout");
        return 1;
    }

    debug_serial_ok("ping");
    return 0;
}

struct debug_command cmd_ping = {
    .name = "ping",
    .handler = cmd_ping_handler,
    .usage = "ping [-c COUNT] <IP|HOSTNAME>",
    .description = "Send ICMP Echo Request"
};

