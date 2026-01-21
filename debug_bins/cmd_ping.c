/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: ping
 * Copyright (C) 2025 Iván Rodriguez
 *
 * Ping command - Send ICMP Echo Request (uses only syscalls)
 */

#include "debug_bins.h"
#include <ir0/fcntl.h>
#include <string.h>
#include <stdbool.h>

/* ntohl is already defined in ir0/net.h */
/* Note: This command uses ONLY syscalls - no kernel function calls */

static int cmd_ping_handler(int argc, char **argv)
{
    if (argc < 2)
    {
        debug_write_err("Usage: ping <IP_ADDRESS_OR_HOSTNAME>\n");
        debug_write_err("Example: ping 192.168.1.1\n");
        debug_write_err("Example: ping www.google.com\n");
        return 1;
    }
    
    const char *target = argv[1];
    
    /* Send ping via /dev/net */
    int64_t ret = ir0_ping(target);
    if (ret < 0)
    {
        debug_write_err("ping: failed to send ICMP echo request\n");
        debug_write_err("Note: If using hostname, ensure DNS is configured\n");
        return 1;
    }
    
    /* Display ping header in Linux format */
    char header[256];
    int header_len = snprintf(header, sizeof(header), "PING %s\n", target);
    if (header_len > 0 && header_len < (int)sizeof(header))
    {
        debug_write(header);
    }
    
    /* Open /dev/net for ioctl operations */
    int fd = syscall(SYS_OPEN, (uint64_t)"/dev/net", O_RDONLY, 0);
    if (fd < 0)
    {
        debug_write_err("ping: failed to open /dev/net\n");
        return 1;
    }
    
    /* Wait for response using ioctl */
    struct ping_result result;
    bool got_response = false;
    int timeout_attempts = 5000;
    int attempt = 0;
    
    /* Poll network periodically while waiting for response */
    /* Note: In real userspace, this would be done via select/poll syscall on /dev/net */
    /* For now, we simulate by using a small delay - network will be polled by kernel main loop */
    while (attempt < timeout_attempts && !got_response)
    {
        /* TODO: Use select/poll syscall on /dev/net when implemented */
        /* For now, rely on kernel's background network polling */
        
        /* Check for ping result via ioctl */
        int64_t ioctl_ret = syscall(SYS_IOCTL, fd, NET_GET_PING_RESULT, (uint64_t)&result);
        if (ioctl_ret == 0 && result.success == 1)
        {
            got_response = true;
            
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
            
            /* Format RTT */
            if (result.rtt < 100)
            {
                snprintf(rtt_str, sizeof(rtt_str), "%llu", (unsigned long long)result.rtt);
            }
            else
            {
                snprintf(rtt_str, sizeof(rtt_str), "%llu.%llu", 
                        (unsigned long long)(result.rtt / 10), (unsigned long long)(result.rtt % 10));
            }
            
            int output_len = snprintf(output, sizeof(output),
                "%d bytes from %d.%d.%d.%d: icmp_seq=0 ttl=%d time=%s ms\n",
                (int)result.payload_bytes, ip[0], ip[1], ip[2], ip[3], (int)result.ttl, rtt_str);
            
            if (output_len > 0 && output_len < (int)sizeof(output))
            {
                debug_write(output);
            }
        }
        
        attempt++;
        
        /* Delay to allow network processing */
        volatile int delay = 10000;
        while (delay-- > 0) { /* busy-wait delay */ }
    }
    
    syscall(SYS_CLOSE, fd, 0, 0);
    
    if (!got_response)
    {
        debug_write_err("ping: Request timeout (no response received)\n");
        return 1;
    }
    
    return 0;
}

struct debug_command cmd_ping = {
    .name = "ping",
    .handler = cmd_ping_handler,
    .usage = "ping <IP|HOSTNAME>",
    .description = "Send ICMP Echo Request"
};

