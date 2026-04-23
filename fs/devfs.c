/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Virtual Device Filesystem (/dev)
 * Copyright (C) 2025 Iván Rodriguez
 *
 */

#include "devfs.h"
#include <mm/allocator.h>
#include <ir0/kmem.h>
#include <ir0/vga.h>
#include <ir0/logging.h>
#include <ir0/keyboard.h>
#include <ir0/errno.h>
#include <config.h>
#include <drivers/video/typewriter.h>
#if CONFIG_ENABLE_SOUND
#include <drivers/audio/sound_blaster.h>
#endif
#include <drivers/IO/ps2_mouse.h>
#include <net/rtl8139.h>
#include <net/arp.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <net/dns.h>
#include <ir0/net.h>
#include <kernel/syscalls.h>
#include <drivers/storage/ata.h>
#include <drivers/disk/partition.h>
#include <string.h>
#include <drivers/timer/clock_system.h>
#include "kernel/ipc.h"
#if CONFIG_ENABLE_BLUETOOTH
#include "drivers/bluetooth/bt_device.h"
#endif
#include <drivers/video/vbe.h>
#include <ir0/copy_user.h>
#include <ir0/input.h>
#include <drivers/serial/serial.h>

/* Device registry */
#define MAX_DEV_NODES 64
static devfs_node_t *dev_nodes[MAX_DEV_NODES];
static int num_dev_nodes = 0;

/*
 * Disk/partition device IDs: 20 = hda, 21 = hdb, 22 = hdc, 23 = hdd (whole disk);
 * 24 = hda1, 25 = hda2, ... 27 = hda4, 28 = hdb1, ... 39 = hdd4.
 */
#define DEVFS_DISK_AGGREGATE_ID  9
#define DEVFS_DISK_BASE_ID      20
#define DEVFS_DISK_STRIDE       4

static void devfs_decode_disk_device_id(uint32_t device_id, uint8_t *disk_id_out, uint8_t *part_out)
{
    if (device_id < DEVFS_DISK_BASE_ID || device_id > DEVFS_DISK_BASE_ID + 19)
    {
        *disk_id_out = 0;
        *part_out = 0;
        return;
    }
    if (device_id < DEVFS_DISK_BASE_ID + 4)
    {
        *disk_id_out = (uint8_t)(device_id - DEVFS_DISK_BASE_ID);
        *part_out = 0;
        return;
    }
    uint32_t idx = device_id - DEVFS_DISK_BASE_ID - 4;
    *disk_id_out = (uint8_t)(idx / 4);
    *part_out = (uint8_t)(idx % 4 + 1);
}

int64_t dev_null_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)buf; (void)count; (void)offset;
    /* Always returns EOF */
    return 0;
}

int64_t dev_null_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)buf; (void)offset;
    /* Accepts all data, discards it */
    return count;
}

int64_t dev_zero_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    memset(buf, 0, count);
    return count;
}

int64_t dev_zero_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)buf; (void)offset;
    return count;
}

int64_t dev_console_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    char *buffer = (char *)buf;
    size_t bytes_read = 0;
    
    /* Read characters from keyboard buffer */
    while (bytes_read < count && keyboard_buffer_has_data())
    {
        char c = keyboard_buffer_get();
        if (c != 0)
        {
            buffer[bytes_read++] = c;
        }
        else
        {
            /* No more data available */
            break;
        }
    }
    
    return (int64_t)bytes_read;
}

int64_t dev_console_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    const char *str = (const char *)buf;
    for (size_t i = 0; i < count && i < 1024; i++)
    {
        if (str[i] == '\n')
            typewriter_vga_print("\n", 0x0F);
        else
            typewriter_vga_print_char(str[i], 0x0F);
    }
    return count;
}

/* Circular buffer for kernel messages */
#define KMSG_BUFFER_SIZE 4096
static char kmsg_buffer[KMSG_BUFFER_SIZE];
static size_t kmsg_head = 0;
static size_t kmsg_tail = 0;
static size_t kmsg_count = 0;  /* Number of bytes in buffer */

int64_t dev_kmsg_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    const char *msg = (const char *)buf;
    size_t written = 0;
    
    /* Add to circular buffer */
    for (size_t i = 0; i < count && i < 256; i++)
    {
        kmsg_buffer[kmsg_head] = msg[i];
        kmsg_head = (kmsg_head + 1) % KMSG_BUFFER_SIZE;
        
        if (kmsg_count < KMSG_BUFFER_SIZE)
        {
            kmsg_count++;
        }
        else
        {
            /* Buffer is full, overwrite oldest data */
            kmsg_tail = (kmsg_tail + 1) % KMSG_BUFFER_SIZE;
        }
        written++;
    }
    
    return (int64_t)written;
}

int64_t dev_kmsg_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    char *out_buf = (char *)buf;
    size_t read_count = 0;
    
    /* Read from circular buffer */
    while (read_count < count && kmsg_count > 0)
    {
        out_buf[read_count] = kmsg_buffer[kmsg_tail];
        kmsg_tail = (kmsg_tail + 1) % KMSG_BUFFER_SIZE;
        kmsg_count--;
        read_count++;
    }
    
    return (int64_t)read_count;
}

#if CONFIG_ENABLE_SOUND
/*
 * PCM format state for /dev/audio. Default: Doom-compatible 11025 Hz, 8-bit mono.
 * Use ioctl(AUDIO_SET_FORMAT) before write() to change.
 */
static struct audio_format audio_pcm_format = {
    .sample_rate = 11025,
    .channels = 1,
    .bits_per_sample = 8
};
#endif

int64_t dev_audio_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry;
    (void)offset;
#if CONFIG_ENABLE_SOUND
    if (!sb16_is_available())
    {
        /* Sound Blaster not available, accept data but don't process */
        return (int64_t)count;
    }
    if (!buf || count == 0)
        return (int64_t)count;
    
    sb16_sample_t sample;
    if (sb16_create_sample(&sample, (uint8_t *)buf, (uint32_t)count,
                           audio_pcm_format.sample_rate,
                           audio_pcm_format.channels,
                           audio_pcm_format.bits_per_sample) != 0)
        return -1;
    int ret = sb16_play_sample(&sample);
    sb16_destroy_sample(&sample);
    return (ret == 0) ? (int64_t)count : -1;
#else
    (void)buf; (void)count;
    return -ENODEV;
#endif
}

int64_t dev_audio_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
    (void)entry;
#if CONFIG_ENABLE_SOUND
    if (!sb16_is_available())
    {
        return -1;  /* Device not available */
    }
    
    switch (request)
    {
        case AUDIO_SET_VOLUME:
            if (arg)
            {
                uint8_t volume = *(uint8_t *)arg;
                if (volume > 100)
                    volume = 100;  /* Clamp to 0-100 */
                /* Convert 0-100 to 0x00-0xFF mixer value */
                uint8_t mixer_vol = (volume * 255) / 100;
                sb16_set_master_volume(mixer_vol);
                return 0;
            }
            return -1;
            
        case AUDIO_GET_VOLUME:
            if (arg)
            {
                /* Read volume from mixer register */
                /* Note: Mixer register returns two 4-bit values (left/right) */
                /* We read and combine them, or use the PCM volume */
                uint8_t mixer_vol = sb16_mixer_read(SB16_MIXER_MASTER_VOL);
                /* Mixer value format: bits 7-4 = left, bits 3-0 = right */
                /* Extract average or use one channel */
                uint8_t left = (mixer_vol >> 4) & 0x0F;
                uint8_t right = mixer_vol & 0x0F;
                uint8_t avg = (left + right) / 2;
                /* Convert 4-bit (0-15) to 0-100, then scale to 0x00-0xFF equivalent */
                uint8_t volume = (avg * 100) / 15;
                *(uint8_t *)arg = volume;
                return 0;
            }
            return -1;
            
        case AUDIO_PLAY:
            /* Speaker should already be on via dev_audio_write */
            sb16_speaker_on();
            return 0;
            
        case AUDIO_STOP:
            /* Stop playback by turning off speaker */
            sb16_speaker_off();
            return 0;
            
        case AUDIO_SET_FORMAT:
            if (arg)
            {
                struct audio_format fmt;
                if (copy_from_user(&fmt, arg, sizeof(fmt)) != 0)
                    return -1;
                /* SB16 8-bit mono: 4000-45454 Hz. Doom uses 11025. */
                if (fmt.sample_rate < 4000 || fmt.sample_rate > 45454)
                    return -1;
                if (fmt.channels != 1 || fmt.bits_per_sample != 8)
                    return -1;  /* 16-bit/stereo not yet implemented */
                audio_pcm_format = fmt;
                return 0;
            }
            return -1;
            
        case AUDIO_GET_FORMAT:
            if (arg)
            {
                if (copy_to_user(arg, &audio_pcm_format, sizeof(audio_pcm_format)) != 0)
                    return -1;
                return 0;
            }
            return -1;
            
        default:
            return -1;  /* Invalid request */
    }
#else
    (void)request; (void)arg;
    return -ENODEV;
#endif
}

int64_t dev_audio_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)buf; (void)count; (void)offset;
    /* Audio input not implemented yet */
    return 0;
}

int64_t dev_mouse_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    if (count < sizeof(int) * 3)
        return 0;
    
    /* Return mouse state: x, y, buttons */
    int *mouse_data = (int *)buf;
    
    if (ps2_mouse_is_available())
    {
        ps2_mouse_state_t *st = ps2_mouse_get_state();
        mouse_data[0] = (int)st->x;
        mouse_data[1] = (int)st->y;
        /* Encode button state: L=bit0, R=bit1, M=bit2 */
        mouse_data[2] = (st->left_button ? 1 : 0) |
                       (st->right_button ? 2 : 0) |
                       (st->middle_button ? 4 : 0);
    }
    else
    {
        mouse_data[0] = 0;
        mouse_data[1] = 0;
        mouse_data[2] = 0;
    }
    
    return sizeof(int) * 3;
}

int64_t dev_mouse_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
    (void)entry;
    
    if (!ps2_mouse_is_available())
    {
        return -1;  /* Device not available */
    }
    
    switch (request)
    {
        case MOUSE_GET_STATE:
            if (arg)
            {
                ps2_mouse_state_t *st = ps2_mouse_get_state();
                if (st)
                {
                    ps2_mouse_state_t *out = (ps2_mouse_state_t *)arg;
                    *out = *st;  /* Copy state */
                    return 0;
                }
            }
            return -1;
            
        case MOUSE_SET_SENSITIVITY:
            if (arg)
            {
                uint8_t sensitivity = *(uint8_t *)arg;
                /* Sensitivity maps to sample rate: higher = more sensitive */
                /* Typical range: 10-200 samples/sec, default 100 */
                if (sensitivity < 10)
                    sensitivity = 10;
                if (sensitivity > 200)
                    sensitivity = 200;
                if (ps2_mouse_set_sample_rate(sensitivity))
                {
                    return 0;
                }
            }
            return -1;
            
        default:
            return -1;  /* Invalid request */
    }
}

int64_t dev_net_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    const char *cmd = (const char *)buf;
    
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
            
#ifdef IR0_TAP_NETWORKING
            /* TAP networking: Use Google DNS (8.8.8.8) directly
             * The gateway (192.168.100.1) is not a DNS server, so we use 8.8.8.8 directly
             */
            ip4_addr_t dns_server = htonl((8 << 24) | (8 << 16) | (8 << 8) | 8);  /* 8.8.8.8 */
            LOG_INFO_FMT("DEVNET", "Attempting DNS resolution for '%s' using 8.8.8.8", hostname);
            dest_ip = dns_resolve(hostname, dns_server);
#else
            /* QEMU user-mode: Use QEMU's default DNS (10.0.2.3) */
            ip4_addr_t dns_server = htonl((10 << 24) | (0 << 16) | (2 << 8) | 3);  /* 10.0.2.3 */
            LOG_INFO_FMT("DEVNET", "Attempting DNS resolution for '%s' using 10.0.2.3", hostname);
            
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
#endif
            
            if (dest_ip == 0)
            {
                /* DNS resolution failed */
                LOG_INFO_FMT("DEVNET", "DNS resolution failed for '%s'", hostname);
                return -1;
            }
            else
            {
                LOG_INFO_FMT("DEVNET", "DNS resolution successful: '%s' -> resolved IP", hostname);
            }
        }
        
        /* Send ping via ioctl */
        return dev_net_ioctl(entry, NET_SEND_PING, &dest_ip);
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
            typedef struct {
                ip4_addr_t *ip;
                ip4_addr_t *netmask;
                ip4_addr_t *gateway;
            } net_config_t;
            
            ip4_addr_t ip, netmask, gateway;
            net_config_t config = { &ip, &netmask, &gateway };
            
            if (dev_net_ioctl(entry, NET_GET_CONFIG, &config) == 0)
            {
                /* Format and display configuration */
                char buf[256];
                
                /* Format IP addresses */
                uint32_t ip_h = ntohl(ip);
                uint32_t netmask_h = ntohl(netmask);
                uint32_t gateway_h = ntohl(gateway);
                
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
                typewriter_vga_print(buf, 0x0F);
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
                        return -1;
                }
                else if (*parse_ptr == '.')
                {
                    ip_octets[octet_idx++] = (uint8_t)value;
                    value = 0;
                }
                else
                    return -1;
                parse_ptr++;
            }
            if (octet_idx == 3)
                ip_octets[octet_idx] = (uint8_t)value;
            else
                return -1;
            
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
                            return -1;
                    }
                    else if (*parse_ptr == '.')
                    {
                        netmask_octets[octet_idx++] = (uint8_t)value;
                        value = 0;
                    }
                    else
                        return -1;
                    parse_ptr++;
                }
                if (octet_idx == 3)
                    netmask_octets[octet_idx] = (uint8_t)value;
                else
                    return -1;
                
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
                            return -1;
                    }
                    else if (*parse_ptr == '.')
                    {
                        gateway_octets[octet_idx++] = (uint8_t)value;
                        value = 0;
                    }
                    else
                        return -1;
                    parse_ptr++;
                }
                if (octet_idx == 3)
                    gateway_octets[octet_idx] = (uint8_t)value;
                else
                    return -1;
                
                new_gateway = htonl((gateway_octets[0] << 24) | (gateway_octets[1] << 16) | 
                                    (gateway_octets[2] << 8) | gateway_octets[3]);
            }
            
            /* Set configuration via ioctl */
            typedef struct {
                ip4_addr_t ip;
                ip4_addr_t netmask;
                ip4_addr_t gateway;
            } net_set_config_t;
            
            net_set_config_t config = {
                .ip = new_ip,
                .netmask = new_netmask,
                .gateway = new_gateway
            };
            
            return dev_net_ioctl(entry, NET_SET_CONFIG, &config);
        }
    }
    
    return count;
}

int64_t dev_net_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    
    if (!buf || count == 0)
        return 0;
    
    /* Poll network to process incoming packets before checking for results
     * This ensures that ICMP replies are processed even during the wait loop
     */
    net_poll();
    
    /* Read ping result if available */
    /* Format: "success:1 rtt:12 ttl:64 payload:32 ip:192.168.100.1" or "success:0" if no response */
    
    int64_t pid = sys_getpid();
    uint16_t id = (uint16_t)(pid & 0xFFFF);
    uint16_t seq = 0;
    
    uint64_t rtt = 0;
    uint8_t ttl = 0;
    size_t payload_bytes = 0;
    ip4_addr_t reply_ip = 0;
    
    char result_buf[256];
    int len = 0;
    
    if (icmp_get_echo_result(id, seq, &rtt, &ttl, &payload_bytes, &reply_ip))
    {
        /* Format result as text for POSIX read() */
        uint32_t ip = ntohl(reply_ip);
        uint8_t ip1 = (ip >> 24) & 0xFF;
        uint8_t ip2 = (ip >> 16) & 0xFF;
        uint8_t ip3 = (ip >> 8) & 0xFF;
        uint8_t ip4 = ip & 0xFF;
        
        char rtt_str[16], ttl_str[16], bytes_str[16];
        char ip1_str[16], ip2_str[16], ip3_str[16], ip4_str[16];
        
        itoa((int)rtt, rtt_str, 10);
        itoa((int)ttl, ttl_str, 10);
        itoa((int)payload_bytes, bytes_str, 10);
        itoa((int)ip1, ip1_str, 10);
        itoa((int)ip2, ip2_str, 10);
        itoa((int)ip3, ip3_str, 10);
        itoa((int)ip4, ip4_str, 10);
        
        len = snprintf(result_buf, sizeof(result_buf), 
                      "success:1 rtt:%s ttl:%s payload:%s ip:%s.%s.%s.%s\n",
                      rtt_str, ttl_str, bytes_str, ip1_str, ip2_str, ip3_str, ip4_str);
        if (len > 0 && len <= (int)count)
        {
            memcpy(buf, result_buf, (size_t)len);
            return len;
        }
        return (int)count;
    }
    
    /*
     * No ping result: return real network data (interfaces + IP config)
     * so that read(/dev/net) always exposes current state.
     */
    {
        char *out = (char *)buf;
        size_t out_len = count;
        size_t off = 0;
        struct net_device *dev = net_get_devices();
        int n;
        n = snprintf(out + off, (off < out_len) ? (out_len - off) : 0,
                     "NAME MTU FLAGS MAC\n");
        if (n > 0 && (size_t)n < out_len - off)
            off += (size_t)n;
        while (dev && off < out_len - 1)
        {
            char flags[32];
            size_t foff = 0;
            flags[0] = '\0';
            if (dev->flags & IFF_UP)
                foff += (size_t)snprintf(flags + foff, sizeof(flags) - foff, "UP");
            if (dev->flags & IFF_RUNNING)
                foff += (size_t)snprintf(flags + foff, sizeof(flags) - foff, "%sRUNNING", foff ? "," : "");
            if (dev->flags & IFF_BROADCAST)
                foff += (size_t)snprintf(flags + foff, sizeof(flags) - foff, "%sBROADCAST", foff ? "," : "");
            if (foff == 0)
                snprintf(flags, sizeof(flags), "-");
            n = snprintf(out + off, out_len - off, "%s %u %s %02x:%02x:%02x:%02x:%02x:%02x\n",
                         dev->name ? dev->name : "", (unsigned)dev->mtu, flags,
                         (unsigned)dev->mac[0], (unsigned)dev->mac[1], (unsigned)dev->mac[2],
                         (unsigned)dev->mac[3], (unsigned)dev->mac[4], (unsigned)dev->mac[5]);
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
                         "IP %u.%u.%u.%u Netmask %u.%u.%u.%u Gateway %u.%u.%u.%u\n",
                         (unsigned)((ip_h >> 24) & 0xFF), (unsigned)((ip_h >> 16) & 0xFF),
                         (unsigned)((ip_h >> 8) & 0xFF), (unsigned)(ip_h & 0xFF),
                         (unsigned)((nm_h >> 24) & 0xFF), (unsigned)((nm_h >> 16) & 0xFF),
                         (unsigned)((nm_h >> 8) & 0xFF), (unsigned)(nm_h & 0xFF),
                         (unsigned)((gw_h >> 24) & 0xFF), (unsigned)((gw_h >> 16) & 0xFF),
                         (unsigned)((gw_h >> 8) & 0xFF), (unsigned)(gw_h & 0xFF));
            if (n > 0 && (size_t)n < out_len - off)
                off += (size_t)n;
        }
        if (off < out_len)
            out[off] = '\0';
        return off > 0 ? (int64_t)off : 0;
    }
}

int64_t dev_net_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
    (void)entry;
    
    /* Network device may not be available, return -1 if needed */
    
    switch (request)
    {
        case NET_SEND_PING:
            if (arg)
            {
                /* arg points to ip4_addr_t */
                ip4_addr_t dest_ip = *(ip4_addr_t *)arg;
                struct net_device *dev = net_get_devices();
                
                if (dev)
                {
                    
                    /* Use process ID as identifier, sequence 0 */
                    pid_t pid = (pid_t)sys_getpid();
                    uint16_t id = (uint16_t)(pid & 0xFFFF);
                    uint16_t seq = 0;
                    
                    int ret = icmp_send_echo_request(dev, dest_ip, id, seq, NULL, 0);
                    return (ret == 0) ? 0 : -1;
                }
                return -1;
            }
            return -1;
            
        case NET_GET_CONFIG:
            if (arg)
            {
                /* arg points to: { ip4_addr_t *ip; ip4_addr_t *netmask; ip4_addr_t *gateway; } */
                typedef struct {
                    ip4_addr_t *ip;
                    ip4_addr_t *netmask;
                    ip4_addr_t *gateway;
                } net_config_t;
                
                net_config_t *config = (net_config_t *)arg;
                if (config)
                {
                    if (config->ip)
                        *config->ip = ip_local_addr;
                    if (config->netmask)
                        *config->netmask = ip_netmask;
                    if (config->gateway)
                        *config->gateway = ip_gateway;
                    return 0;
                }
            }
            return -1;
            
        case NET_SET_CONFIG:
            if (arg)
            {
                /* arg points to: { ip4_addr_t ip; ip4_addr_t netmask; ip4_addr_t gateway; } */
                typedef struct {
                    ip4_addr_t ip;
                    ip4_addr_t netmask;
                    ip4_addr_t gateway;
                } net_config_t;
                
                net_config_t *config = (net_config_t *)arg;
                if (config)
                {
                    ip_local_addr = config->ip;
                    ip_netmask = config->netmask;
                    ip_gateway = config->gateway;
                    arp_set_my_ip(config->ip);  /* Update ARP cache */
                    return 0;
                }
            }
            return -1;
            
        case NET_GET_PING_RESULT:
            if (arg)
            {
                /* arg points to: { int success; uint64_t rtt; uint8_t ttl; size_t payload_bytes; ip4_addr_t reply_ip; } */
                struct ping_result *result = (struct ping_result *)arg;
                if (!result)
                    return -1;
                
                /* Get PID to use as ICMP ID (matches NET_SEND_PING behavior) */
                pid_t pid = (pid_t)sys_getpid();
                uint16_t id = (uint16_t)(pid & 0xFFFF);
                uint16_t seq = 0;
                
                /* Try to get echo result */
                if (icmp_get_echo_result(id, seq, &result->rtt, &result->ttl, 
                                        &result->payload_bytes, &result->reply_ip))
                {
                    result->success = 1;
                    return 0;
                }
                else
                {
                    result->success = 0;
                    return 0;  /* Still pending, but not an error */
                }
            }
            return -1;
            
        default:
            return -1;  /* Invalid request */
    }
}

int64_t dev_disk_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    if (!buf)
        return -1;
    if (entry->device_id != DEVFS_DISK_AGGREGATE_ID)
    {
        uint8_t disk_id, part;
        devfs_decode_disk_device_id(entry->device_id, &disk_id, &part);
        if (!ata_drive_present(disk_id))
            return -ENODEV;
        if (offset < 0)
            return -1;
        uint64_t sector_off = (uint64_t)((unsigned long)offset / 512);
        size_t num_sectors = (count + 511) / 512;
        if (num_sectors == 0)
            return 0;
        uint64_t start_lba = sector_off;
        if (part != 0)
        {
            partition_info_t info;
            if (get_partition_info(disk_id, (uint8_t)(part - 1), &info) != 0)
                return -ENODEV;
            if (sector_off >= info.total_sectors)
                return 0;
            start_lba = info.start_lba + sector_off;
            if (num_sectors > info.total_sectors - sector_off)
                num_sectors = (size_t)(info.total_sectors - sector_off);
        }
        else
        {
            uint64_t disk_sectors = ata_get_size(disk_id);
            if (sector_off >= disk_sectors)
                return 0;
            if (sector_off + num_sectors > disk_sectors)
                num_sectors = (size_t)(disk_sectors - sector_off);
        }
        char *dst = (char *)buf;
        size_t bytes_done = 0;
        while (num_sectors > 0)
        {
            uint8_t n = (num_sectors > 255) ? 255 : (uint8_t)num_sectors;
            if (!ata_read_sectors(disk_id, (uint32_t)start_lba, n, dst))
                return (int64_t)bytes_done;
            bytes_done += (size_t)n * 512;
            start_lba += n;
            num_sectors -= n;
            dst += (size_t)n * 512;
        }
        return (int64_t)((bytes_done < count) ? bytes_done : count);
    }
    /* Aggregate /dev/disk: generate listing */
    
    /* Generate df-like output: Filesystem          Size */
    /* Support offset for seeking within output */
    char output[1024];
    size_t output_len = 0;
    
    /* Validate offset */
    if (offset < 0)
        return -1;
    
    int n = snprintf(output + output_len, sizeof(output) - output_len,
                     "Filesystem          Size\n");
    if (n > 0 && (size_t)n < sizeof(output) - output_len)
        output_len += (size_t)n;
    
    n = snprintf(output + output_len, sizeof(output) - output_len,
                 "----------------------------------\n");
    if (n > 0 && (size_t)n < sizeof(output) - output_len)
        output_len += (size_t)n;
    
    int found_drives = 0;
    for (uint8_t i = 0; i < 4; i++)
    {
        if (!ata_drive_present(i))
            continue;
        
        found_drives++;
        char devname[16];
        int len = snprintf(devname, sizeof(devname), "/dev/hd%c", 'a' + i);
        if (len < 0 || len >= (int)sizeof(devname))
            continue;
        
        /* ata_get_size() returns size in 512-byte sectors */
        uint64_t size = ata_get_size(i);
        if (size == 0)
        {
            n = snprintf(output + output_len, sizeof(output) - output_len,
                        "%-20s (empty)\n", devname);
        }
        else
        {
            char size_str[32];
            /* Same calculation: sectors / (2 * 1024 * 1024) = GB */
            uint64_t size_gb = size / (2 * 1024 * 1024);
            if (size_gb > 0)
            {
                /* Convert uint64_t to string manually since snprintf doesn't support %llu */
                char num_str[32];
                char *p = num_str;
                uint64_t tmp = size_gb;
                if (tmp == 0)
                {
                    *p++ = '0';
                }
                else
                {
                    char rev[32];
                    int idx = 0;
                    while (tmp > 0)
                    {
                        rev[idx++] = '0' + (tmp % 10);
                        tmp /= 10;
                    }
                    while (idx > 0)
                        *p++ = rev[--idx];
                }
                *p = '\0';
                len = snprintf(size_str, sizeof(size_str), "%sG", num_str);
            }
            else
            {
                /* sectors / (2 * 1024) = MB */
                uint64_t size_mb = size / (2 * 1024);
                char num_str[32];
                char *p = num_str;
                uint64_t tmp = size_mb;
                if (tmp == 0)
                {
                    *p++ = '0';
                }
                else
                {
                    char rev[32];
                    int idx = 0;
                    while (tmp > 0)
                    {
                        rev[idx++] = '0' + (tmp % 10);
                        tmp /= 10;
                    }
                    while (idx > 0)
                        *p++ = rev[--idx];
                }
                *p = '\0';
                len = snprintf(size_str, sizeof(size_str), "%sM", num_str);
            }
            
            if (len > 0 && len < (int)sizeof(size_str))
            {
                n = snprintf(output + output_len, sizeof(output) - output_len,
                            "%-20s %s\n", devname, size_str);
            }
            else
            {
                n = 0;
            }
        }
        
        if (n > 0 && (size_t)n < sizeof(output) - output_len)
            output_len += (size_t)n;
    }
    
    if (found_drives == 0)
    {
        n = snprintf(output + output_len, sizeof(output) - output_len,
                    "No drives detected\n");
        if (n > 0 && (size_t)n < sizeof(output) - output_len)
            output_len += (size_t)n;
    }
    
    /* Support offset: skip bytes if offset is beyond start */
    size_t start_pos = (size_t)offset;
    if (start_pos >= output_len)
    {
        /* Offset beyond end of output - return empty */
        return 0;
    }
    
    /* Copy to user buffer (respecting offset and count) */
    size_t available = output_len - start_pos;
    size_t copy_size = (available < count) ? available : count;
    if (copy_size > 0)
    {
        memcpy(buf, output + start_pos, copy_size);
    }
    
    return (int64_t)copy_size;
}

int64_t dev_disk_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    if (entry->device_id == DEVFS_DISK_AGGREGATE_ID)
        return count;
    if (!buf)
        return -1;
    uint8_t disk_id, part;
    devfs_decode_disk_device_id(entry->device_id, &disk_id, &part);
    if (!ata_drive_present(disk_id))
        return -ENODEV;
    if (offset < 0)
        return -1;
    uint64_t sector_off = (uint64_t)((unsigned long)offset / 512);
    size_t num_sectors = (count + 511) / 512;
    if (num_sectors == 0)
        return 0;
    uint64_t start_lba = sector_off;
    if (part != 0)
    {
        partition_info_t info;
        if (get_partition_info(disk_id, (uint8_t)(part - 1), &info) != 0)
            return -ENODEV;
        if (sector_off >= info.total_sectors)
            return 0;
        start_lba = info.start_lba + sector_off;
        if (num_sectors > info.total_sectors - sector_off)
            num_sectors = (size_t)(info.total_sectors - sector_off);
    }
    else
    {
        uint64_t disk_sectors = ata_get_size(disk_id);
        if (sector_off >= disk_sectors)
            return 0;
        if (sector_off + num_sectors > disk_sectors)
            num_sectors = (size_t)(disk_sectors - sector_off);
    }
    const char *src = (const char *)buf;
    size_t bytes_done = 0;
    while (num_sectors > 0)
    {
        uint8_t n = (num_sectors > 255) ? 255 : (uint8_t)num_sectors;
        if (!ata_write_sectors(disk_id, (uint32_t)start_lba, n, src))
            return (int64_t)bytes_done;
        bytes_done += (size_t)n * 512;
        start_lba += n;
        num_sectors -= n;
        src += (size_t)n * 512;
    }
    return (int64_t)((bytes_done < count) ? bytes_done : count);
}

int64_t dev_disk_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
    uint8_t disk_id = 0;
    uint8_t part = 0;
    uint64_t part_start_lba = 0;
    uint64_t part_sectors = 0;
    if (entry->device_id >= DEVFS_DISK_BASE_ID && entry->device_id <= DEVFS_DISK_BASE_ID + 19)
    {
        devfs_decode_disk_device_id(entry->device_id, &disk_id, &part);
        if (!ata_drive_present(disk_id))
            return -ENODEV;
        if (part != 0)
        {
            partition_info_t info;
            if (get_partition_info(disk_id, (uint8_t)(part - 1), &info) != 0)
                return -ENODEV;
            part_start_lba = info.start_lba;
            part_sectors = info.total_sectors;
        }
    }
    
    switch (request)
    {
        case DISK_READ_SECTOR:
            if (arg)
            {
                typedef struct {
                    uint8_t drive;
                    uint32_t lba;
                    void *buffer;
                } disk_sector_req_t;
                disk_sector_req_t *req = (disk_sector_req_t *)arg;
                if (req && req->buffer)
                {
                    uint8_t d = (entry->device_id >= DEVFS_DISK_BASE_ID) ? disk_id : req->drive;
                    uint32_t lba = (uint32_t)req->lba;
                    if (part != 0)
                        lba = (uint32_t)(part_start_lba + req->lba);
                    if (ata_drive_present(d) && ata_read_sectors(d, lba, 1, req->buffer))
                        return 512;
                }
            }
            return -1;
            
        case DISK_WRITE_SECTOR:
            if (arg)
            {
                typedef struct {
                    uint8_t drive;
                    uint32_t lba;
                    const void *buffer;
                } disk_sector_req_t;
                disk_sector_req_t *req = (disk_sector_req_t *)arg;
                if (req && req->buffer)
                {
                    uint8_t d = (entry->device_id >= DEVFS_DISK_BASE_ID) ? disk_id : req->drive;
                    uint32_t lba = (uint32_t)req->lba;
                    if (part != 0)
                        lba = (uint32_t)(part_start_lba + req->lba);
                    if (ata_drive_present(d) && ata_write_sectors(d, lba, 1, req->buffer))
                        return 512;
                }
            }
            return -1;
            
        case DISK_GET_GEOMETRY:
            if (arg)
            {
                typedef struct {
                    uint8_t drive;
                    uint64_t *size_sectors;
                    uint64_t *size_bytes;
                } disk_geometry_t;
                disk_geometry_t *geom = (disk_geometry_t *)arg;
                if (geom)
                {
                    uint8_t d = (entry->device_id >= DEVFS_DISK_BASE_ID) ? disk_id : geom->drive;
                    if (!ata_drive_present(d))
                        return -1;
                    uint64_t sectors = (part != 0) ? part_sectors : ata_get_size(d);
                    if (geom->size_sectors)
                        *geom->size_sectors = sectors;
                    if (geom->size_bytes)
                        *geom->size_bytes = sectors * 512;
                    return 0;
                }
            }
            return -1;
            
        default:
            return -1;
    }
}

/**
 * /dev/random and /dev/urandom - Random number generators
 * 
 * Simple PRNG based on timer ticks and interrupt counters
 * For production, should use hardware RNG or better entropy source
 */
static uint32_t random_seed = 0;

static uint32_t simple_rand(void)
{
    /* Linear congruential generator (LCG) */
    random_seed = (random_seed * 1103515245 + 12345) & 0x7FFFFFFF;
    return random_seed;
}

int64_t dev_random_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    
    /* Initialize seed with timer if not set */
    if (random_seed == 0)
    {
        random_seed = (uint32_t)(clock_get_uptime_milliseconds() & 0xFFFFFFFF);
    }
    
    /* Fill buffer with random bytes */
    uint8_t *buffer = (uint8_t *)buf;
    for (size_t i = 0; i < count; i++)
    {
        buffer[i] = (uint8_t)(simple_rand() & 0xFF);
    }
    
    return (int64_t)count;
}

int64_t dev_random_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    /*
     * /dev/random accepts writes to add entropy to the pool.
     * Mix written bytes with random_seed via XOR and LCG feedback.
     */
    const uint8_t *p = (const uint8_t *)buf;
    for (size_t i = 0; i < count; i++) {
        random_seed ^= (uint32_t)p[i] << (i % 24);
        random_seed = (random_seed * 1103515245 + 12345) & 0x7FFFFFFF;
    }
    return (int64_t)count;
}

int64_t dev_urandom_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    
    /* /dev/urandom: Non-blocking random number generator
     * Unlike /dev/random which may block when entropy is low,
     * /dev/urandom never blocks and continues generating pseudo-random data.
     * 
     * In this implementation, both use the same LCG-based generator,
     * but /dev/urandom explicitly never blocks and always returns immediately.
     */
    
    /* Initialize seed with timer if not set */
    if (random_seed == 0)
    {
        random_seed = (uint32_t)(clock_get_uptime_milliseconds() & 0xFFFFFFFF);
    }
    
    /* Fill buffer with random bytes - non-blocking, always succeeds */
    uint8_t *buffer = (uint8_t *)buf;
    for (size_t i = 0; i < count; i++)
    {
        /* Use LCG with different multiplier for urandom to differentiate streams */
        random_seed = (random_seed * 1664525 + 1013904223) & 0x7FFFFFFF;
        buffer[i] = (uint8_t)(random_seed & 0xFF);
    }
    
    return (int64_t)count;
}

int64_t dev_urandom_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    /* Same as /dev/random */
    return dev_random_write(entry, buf, count, offset);
}

/**
 * /dev/full - Device that always returns ENOSPC on write
 */
int64_t dev_full_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)buf; (void)count; (void)offset;
    /* Reading from /dev/full returns \0 (null bytes) */
    memset(buf, 0, count);
    return (int64_t)count;
}

int64_t dev_full_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)buf; (void)count; (void)offset;
    /* Writing to /dev/full always fails with ENOSPC */
    return -ENOSPC;  /* No space left on device */
}

static const devfs_ops_t null_ops = {
    .read = dev_null_read,
    .write = dev_null_write,
};

static const devfs_ops_t zero_ops = {
    .read = dev_zero_read,
    .write = dev_zero_write,
};

static const devfs_ops_t console_ops = {
    .read = dev_console_read,
    .write = dev_console_write,
};

static const devfs_ops_t kmsg_ops = {
    .read = dev_kmsg_read,
    .write = dev_kmsg_write,
};

static const devfs_ops_t audio_ops = {
    .read = dev_audio_read,
    .write = dev_audio_write,
    .ioctl = dev_audio_ioctl,
};

static const devfs_ops_t mouse_ops = {
    .read = dev_mouse_read,
    .ioctl = dev_mouse_ioctl,
};

static const devfs_ops_t net_ops = {
    .read = dev_net_read,
    .write = dev_net_write,
    .ioctl = dev_net_ioctl,
};

static const devfs_ops_t disk_ops = {
    .read = dev_disk_read,
    .write = dev_disk_write,
    .ioctl = dev_disk_ioctl,
};

static const devfs_ops_t random_ops = {
    .read = dev_random_read,
    .write = dev_random_write,
};

static const devfs_ops_t urandom_ops = {
    .read = dev_urandom_read,
    .write = dev_urandom_write,
};

static const devfs_ops_t full_ops = {
    .read = dev_full_read,
    .write = dev_full_write,
};

/*
 * /dev/fb0 - Framebuffer (OSDev / Linux fbdev)
 * write: copy pixels to framebuffer
 * ioctl(FBIOGET_VSCREENINFO): get width, height, bpp, pitch
 */
static int64_t dev_fb0_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)buf; (void)count; (void)offset;
    return 0;
}

static int64_t dev_fb0_write_simple(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    uint8_t *fb = vbe_get_fb();
    uint32_t pitch = vbe_get_pitch();
    uint32_t w = 0, h = 0;
    if (!vbe_is_available() || !fb || !pitch)
        return -ENODEV;
    vbe_get_info(&w, &h, NULL);
    uint32_t size = pitch * h;
    if (count > size)
        count = size;
    memcpy(fb, buf, count);
    return (int64_t)count;
}

static int64_t dev_fb0_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
    (void)entry;
    if (!vbe_is_available())
        return -ENODEV;
    if (request == FBIOGET_VSCREENINFO && arg)
    {
        struct fb_var_screeninfo info;
        uint32_t w = 0, h = 0, bpp = 0;
        vbe_get_info(&w, &h, &bpp);
        memset(&info, 0, sizeof(info));
        info.xres = w;
        info.yres = h;
        info.xres_virtual = w;
        info.yres_virtual = h;
        info.bits_per_pixel = bpp;
        if (copy_to_user(arg, &info, sizeof(info)) != 0)
            return -EFAULT;
        return 0;
    }
    if (request == FBIOGET_FSCREENINFO && arg)
    {
        struct fb_fix_screeninfo fix;
        uint32_t w = 0, h = 0, bpp = 0;
        vbe_get_info(&w, &h, &bpp);
        memset(&fix, 0, sizeof(fix));
        strncpy(fix.id, "IR0 VBE", sizeof(fix.id) - 1);
        fix.smem_start = vbe_get_fb_phys();
        fix.smem_len = vbe_get_fb_size();
        fix.type = FB_TYPE_PACKED_PIXELS;
        fix.visual = FB_VISUAL_TRUECOLOR;
        fix.line_length = vbe_get_pitch();
        fix.accel = FB_ACCEL_NONE;
        if (copy_to_user(arg, &fix, sizeof(fix)) != 0)
            return -EFAULT;
        return 0;
    }
    return -EINVAL;
}

static const devfs_ops_t fb0_ops = {
    .read = dev_fb0_read,
    .write = dev_fb0_write_simple,
    .ioctl = dev_fb0_ioctl,
};

static devfs_node_t dev_fb0 = {
    .entry = { .name = "fb0", .mode = 0660, .device_id = 15 },
    .ops = &fb0_ops,
    .ref_count = 0,
};

/*
 * /dev/events0 - Linux evdev input events (keyboard for Doom)
 * read: struct input_event (type, code, value)
 */
static int64_t dev_events0_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    if (count < sizeof(struct input_event))
        return 0;
    struct input_event ev_buf[16];
    size_t max_ev = count / sizeof(struct input_event);
    if (max_ev > 16)
        max_ev = 16;
    size_t n = input_event_read(ev_buf, max_ev);
    if (n == 0)
        return 0;
    size_t bytes = n * sizeof(struct input_event);
    if (copy_to_user(buf, ev_buf, bytes) != 0)
        return -EFAULT;
    return (int64_t)bytes;
}

static const devfs_ops_t events0_ops = {
    .read = dev_events0_read,
    .write = NULL,
    .ioctl = NULL,
};

static devfs_node_t dev_events0 = {
    .entry = { .name = "events0", .mode = 0660, .device_id = 16 },
    .ops = &events0_ops,
    .ref_count = 0,
};

/*
 * /dev/serial - Write-only debug output to COM1 (for debug bins via syscalls)
 */
static int64_t dev_serial_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)buf; (void)count; (void)offset;
    return 0;
}

static int64_t dev_serial_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    if (!buf)
        return -EFAULT;
    const char *p = (const char *)buf;
    for (size_t i = 0; i < count; i++)
        serial_putchar(p[i]);
    return (int64_t)count;
}

static const devfs_ops_t serial_ops = {
    .read = dev_serial_read,
    .write = dev_serial_write,
};

static devfs_node_t dev_serial = {
    .entry = { .name = "serial", .mode = 0220, .device_id = 42 },
    .ops = &serial_ops,
    .ref_count = 0,
};

/* IPC device operations */
int64_t dev_ipc_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    
    /* Extract channel ID from driver_data (set in open/ioctl) */
    ipc_channel_t *channel = (ipc_channel_t *)entry->driver_data;
    if (!channel)
        return -1;
    
    /* Read from IPC channel (blocking) */
    return ipc_channel_read(channel, buf, count);
}

int64_t dev_ipc_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    
    /* Extract channel ID from driver_data (set in open/ioctl) */
    ipc_channel_t *channel = (ipc_channel_t *)entry->driver_data;
    if (!channel)
        return -1;
    
    /* Write to IPC channel (blocking) */
    return ipc_channel_write(channel, buf, count);
}

int64_t dev_ipc_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
    if (!entry)
        return -1;
    
    switch (request)
    {
        case IPC_CREATE_CHANNEL:
            if (arg) {
                /* arg points to uint32_t *channel_id (input/output) */
                uint32_t *channel_id_ptr = (uint32_t *)arg;
                uint32_t channel_id = *channel_id_ptr;
                
                /* If channel_id is 0, allocate a new one */
                if (channel_id == 0) {
                    channel_id = ipc_allocate_channel_id();
                }
                
                /* Get or create channel */
                ipc_channel_t *channel = ipc_channel_get_or_create(channel_id);
                if (!channel)
                    return -1;
                
                /* Store channel pointer in driver_data */
                entry->driver_data = (void *)channel;
                
                /* Return channel ID */
                *channel_id_ptr = channel_id;
                
                /* Increment reference count */
                ipc_channel_ref(channel);
                
                return 0;
            }
            return -1;
            
        case IPC_DESTROY_CHANNEL:
            if (entry->driver_data) {
                ipc_channel_t *channel = (ipc_channel_t *)entry->driver_data;
                ipc_channel_unref(channel);
                entry->driver_data = NULL;
                return 0;
            }
            return -1;
            
        case IPC_GET_CHANNEL_ID:
            if (arg && entry->driver_data) {
                ipc_channel_t *channel = (ipc_channel_t *)entry->driver_data;
                uint32_t *channel_id_ptr = (uint32_t *)arg;
                *channel_id_ptr = channel->id;
                return 0;
            }
            return -1;
            
        default:
            return -1;
    }
}

int64_t dev_ipc_open(devfs_entry_t *entry, int flags)
{
    (void)entry; (void)flags;
    /* Channel will be created/opened via ioctl */
    return 0;
}

int64_t dev_ipc_close(devfs_entry_t *entry)
{
    if (!entry)
        return -1;
    
    /* If channel was opened, release reference */
    if (entry->driver_data) {
        ipc_channel_t *channel = (ipc_channel_t *)entry->driver_data;
        ipc_channel_unref(channel);
        entry->driver_data = NULL;
    }
    
    return 0;
}

static const devfs_ops_t ipc_ops = {
    .read = dev_ipc_read,
    .write = dev_ipc_write,
    .ioctl = dev_ipc_ioctl,
    .open = dev_ipc_open,
    .close = dev_ipc_close,
};

#if CONFIG_ENABLE_BLUETOOTH
/* Bluetooth HCI device operations */
int64_t dev_bluetooth_hci_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    return bt_hci_read((char *)buf, count);
}

int64_t dev_bluetooth_hci_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    return bt_hci_write((const char *)buf, count);
}

int64_t dev_bluetooth_hci_open(devfs_entry_t *entry, int flags)
{
    (void)entry; (void)flags;
    return bt_hci_open();
}

int64_t dev_bluetooth_hci_close(devfs_entry_t *entry)
{
    (void)entry;
    return bt_hci_close();
}

int64_t dev_bluetooth_hci_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
    (void)entry;
    return bt_hci_ioctl((unsigned int)request, (unsigned long)arg);
}

static const devfs_ops_t bluetooth_hci_ops = {
    .read = dev_bluetooth_hci_read,
    .write = dev_bluetooth_hci_write,
    .open = dev_bluetooth_hci_open,
    .close = dev_bluetooth_hci_close,
    .ioctl = dev_bluetooth_hci_ioctl
};
#endif

devfs_node_t dev_null = {
    .entry = { .name = "null", .mode = 0666, .device_id = 1 },
    .ops = &null_ops,
    .ref_count = 0
};

devfs_node_t dev_zero = {
    .entry = { .name = "zero", .mode = 0666, .device_id = 2 },
    .ops = &zero_ops,
    .ref_count = 0
};

devfs_node_t dev_console = {
    .entry = { .name = "console", .mode = 0620, .device_id = 3 },
    .ops = &console_ops,
    .ref_count = 0
};

devfs_node_t dev_tty = {
    .entry = { .name = "tty", .mode = 0620, .device_id = 4 },
    .ops = &console_ops,
    .ref_count = 0
};

/*
 * POSIX stream aliases: stdin was 16 and collided with events0 (evdev).
 * stdin uses 17; stdout/stderr/serial use 40–42 to stay clear of disk IDs 20–39.
 */
devfs_node_t dev_stdin = {
    .entry = { .name = "stdin", .mode = 0620, .device_id = 17 },
    .ops = &console_ops,
    .ref_count = 0
};

devfs_node_t dev_stdout = {
    .entry = { .name = "stdout", .mode = 0620, .device_id = 40 },
    .ops = &console_ops,
    .ref_count = 0
};

devfs_node_t dev_stderr = {
    .entry = { .name = "stderr", .mode = 0620, .device_id = 41 },
    .ops = &console_ops,
    .ref_count = 0
};

devfs_node_t dev_kmsg = {
    .entry = { .name = "kmsg", .mode = 0600, .device_id = 5 },
    .ops = &kmsg_ops,
    .ref_count = 0
};

devfs_node_t dev_audio = {
    .entry = { .name = "audio", .mode = 0660, .device_id = 6 },
    .ops = &audio_ops,
    .ref_count = 0
};

devfs_node_t dev_mouse = {
    .entry = { .name = "mouse", .mode = 0660, .device_id = 7 },
    .ops = &mouse_ops,
    .ref_count = 0
};

devfs_node_t dev_net = {
    .entry = { .name = "net", .mode = 0660, .device_id = 8 },
    .ops = &net_ops,
    .ref_count = 0
};

devfs_node_t dev_disk = {
    .entry = { .name = "disk", .mode = 0660, .device_id = 9 },
    .ops = &disk_ops,
    .ref_count = 0
};

/* Per-disk and per-partition block devices: /dev/hda, /dev/hda1, ... /dev/hdd4 */
static const char *const devfs_block_names[] = {
    "hda", "hdb", "hdc", "hdd",
    "hda1", "hda2", "hda3", "hda4",
    "hdb1", "hdb2", "hdb3", "hdb4",
    "hdc1", "hdc2", "hdc3", "hdc4",
    "hdd1", "hdd2", "hdd3", "hdd4"
};
#define NUM_BLOCK_DISK_NODES (sizeof(devfs_block_names) / sizeof(devfs_block_names[0]))
static devfs_node_t dev_block_nodes[NUM_BLOCK_DISK_NODES];

devfs_node_t dev_random = {
    .entry = { .name = "random", .mode = 0644, .device_id = 10 },
    .ops = &random_ops,
    .ref_count = 0
};

devfs_node_t dev_urandom = {
    .entry = { .name = "urandom", .mode = 0644, .device_id = 11 },
    .ops = &urandom_ops,
    .ref_count = 0
};

devfs_node_t dev_full = {
    .entry = { .name = "full", .mode = 0666, .device_id = 12 },
    .ops = &full_ops,
    .ref_count = 0
};

devfs_node_t dev_ipc = {
    .entry = { .name = "ipc", .mode = 0666, .device_id = 13 },
    .ops = &ipc_ops,
    .ref_count = 0
};

#if CONFIG_ENABLE_BLUETOOTH
devfs_node_t dev_bluetooth_hci0 = {
    .entry = { .name = "bluetooth/hci0", .mode = 0660, .device_id = 14 },
    .ops = &bluetooth_hci_ops,
    .ref_count = 0
};
#endif

/**
 * devfs_register_node - Registra un nodo pre-asignado en devfs
 * @node: Nodo con entry y ops ya configurados
 *
 * Para dispositivos con struct estática. Para dispositivos dinámicos usar
 * devfs_register_device.
 */
int devfs_register_node(devfs_node_t *node)
{
    if (!node || num_dev_nodes >= MAX_DEV_NODES)
        return -1;
    dev_nodes[num_dev_nodes++] = node;
    return 0;
}

int devfs_init(void)
{
    devfs_register_node(&dev_null);
    devfs_register_node(&dev_zero);
    devfs_register_node(&dev_console);
    devfs_register_node(&dev_tty);
    devfs_register_node(&dev_stdin);
    devfs_register_node(&dev_stdout);
    devfs_register_node(&dev_stderr);
    devfs_register_node(&dev_kmsg);
    devfs_register_node(&dev_audio);
    devfs_register_node(&dev_mouse);
    devfs_register_node(&dev_net);
    devfs_register_node(&dev_disk);
    for (size_t i = 0; i < NUM_BLOCK_DISK_NODES; i++)
    {
        dev_block_nodes[i].entry.name = devfs_block_names[i];
        dev_block_nodes[i].entry.mode = 0660;
        dev_block_nodes[i].entry.device_id = (uint32_t)(DEVFS_DISK_BASE_ID + (uint32_t)i);
        dev_block_nodes[i].ops = &disk_ops;
        dev_block_nodes[i].ref_count = 0;
        devfs_register_node(&dev_block_nodes[i]);
    }
    devfs_register_node(&dev_random);
    devfs_register_node(&dev_urandom);
    devfs_register_node(&dev_full);
    devfs_register_node(&dev_fb0);
    devfs_register_node(&dev_events0);
    devfs_register_node(&dev_serial);
    devfs_register_node(&dev_ipc);
#if CONFIG_ENABLE_BLUETOOTH
    devfs_register_node(&dev_bluetooth_hci0);
#endif

    return 0;
}

devfs_node_t *devfs_find_node(const char *path)
{
    if (!path || strncmp(path, "/dev/", 5) != 0)
        return NULL;
    
    const char *name = path + 5;  /* Skip "/dev/" */
    
    for (int i = 0; i < num_dev_nodes; i++)
    {
        if (strcmp(dev_nodes[i]->entry.name, name) == 0)
        {
            return dev_nodes[i];
        }
    }
    
    return NULL;
}

devfs_node_t *devfs_find_node_by_id(uint32_t device_id)
{
    for (int i = 0; i < num_dev_nodes; i++)
    {
        if (dev_nodes[i] && dev_nodes[i]->entry.device_id == device_id)
        {
            return dev_nodes[i];
        }
    }
    return NULL;
}

int devfs_register_device(const char *name, const devfs_ops_t *ops, uint32_t mode)
{
    if (num_dev_nodes >= MAX_DEV_NODES)
        return -1;
    
    devfs_node_t *node = kmalloc(sizeof(devfs_node_t));
    if (!node)
        return -1;
    
    node->entry.name = name;
    node->entry.mode = mode;
    node->entry.device_id = num_dev_nodes + 100;  /* Dynamic IDs */
    node->ops = ops;
    node->ref_count = 0;
    
    dev_nodes[num_dev_nodes++] = node;
    return 0;
}
