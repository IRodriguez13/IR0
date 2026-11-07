/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: devfs.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Virtual Device Filesystem (/dev)
 * Copyright (C) 2025 Iván Rodriguez
 *
 */

#include "devfs.h"
#include "devfs_internal.h"
#include <ir0/kmem.h>
#include <ir0/logging.h>
#include <ir0/errno.h>
#include <config.h>
#include <ir0/console_backend.h>
#include <ir0/console.h>
#if CONFIG_ENABLE_SOUND
#include <ir0/audio_backend.h>
#endif
#include <ir0/input_backend.h>
#if CONFIG_ENABLE_NETWORKING
#include <ir0/net.h>
#endif
#include <ir0/blockdev.h>
#include <ir0/partition.h>
#include <string.h>
#include <ir0/clock.h>
#include <ir0/process.h>
#include <ir0/signals.h>
#include <ir0/arch_cpu.h>
#include <ir0/klog.h>
#include <ir0/serial_io.h>
#include <ir0/ipc.h>
#include <ir0/arch_cpu.h>
#if CONFIG_ENABLE_BLUETOOTH
#include <ir0/bluetooth.h>
#endif
#include <ir0/video_backend.h>
#include <ir0/copy_user.h>
#include <ir0/input.h>
#include <ir0/credentials.h>
#include <ir0/fb.h>
#include <ir0/ktm/userdev.h>
#include <ir0/io_async.h>
#include <linux/kd.h>
#include <linux/keyboard.h>
#include <linux/vt.h>
#include "vfs.h"

pid_t devfs_current_pid(void)
{
    return ir0_current_pid();
}

/* Device registry — room for builtins + dense disk/part topology */
#define MAX_DEV_NODES 224
static devfs_node_t *dev_nodes[MAX_DEV_NODES];
static int num_dev_nodes = 0;

devfs_node_t *devfs_find_node_by_id(uint32_t device_id);

#if CONFIG_ENABLE_NETWORKING
#endif

static int devfs_console_can_read(devfs_entry_t *entry, pid_t pid)
{
    (void)entry;
    (void)pid;
    return ir0_console_poll();
}

#if CONFIG_ENABLE_NETWORKING
static int devfs_net_can_read(devfs_entry_t *entry, pid_t pid)
{
    (void)entry;
    return dev_net_pid_has_ready_ping(pid);
}
#endif

static int devfs_serial_can_read(devfs_entry_t *entry, pid_t pid)
{
    (void)entry;
    (void)pid;
    return 0;
}

static int devfs_serial_can_write(devfs_entry_t *entry, pid_t pid)
{
    (void)entry;
    (void)pid;
    return 1;
}

static int devfs_events0_can_write(devfs_entry_t *entry, pid_t pid)
{
    (void)entry;
    (void)pid;
    return 0;
}

static int devfs_events0_can_read(devfs_entry_t *entry, pid_t pid)
{
    (void)entry;
    (void)pid;
    return ir0_input_poll() ? 1 : 0;
}

static int devfs_poll_default(int (*hook)(devfs_entry_t *, pid_t),
                              devfs_node_t *node, pid_t pid)
{
    if (node && node->ops && hook)
        return hook(&node->entry, pid);
    return 1;
}

int devfs_fd_can_read(uint32_t device_id, pid_t pid)
{
    devfs_node_t *node = devfs_find_node_by_id(device_id);

    return devfs_poll_default(
        node && node->ops ? node->ops->can_read : NULL, node, pid);
}

int devfs_fd_can_write(uint32_t device_id, pid_t pid)
{
    devfs_node_t *node = devfs_find_node_by_id(device_id);

    return devfs_poll_default(
        node && node->ops ? node->ops->can_write : NULL, node, pid);
}

static int g_devfs_read_nonblock;

struct console_vt_backend_ops {
    int (*set_keyboard_mode)(int mode);
    int (*get_keyboard_mode)(void);
};

static const struct console_vt_backend_ops console_vt_backend = {
    .set_keyboard_mode = input_kbd_set_console_mode,
    .get_keyboard_mode = input_kbd_get_console_mode,
};

static struct vt_mode console_vt_mode = { .mode = VT_AUTO };
static int console_display_mode = KD_TEXT;

static unsigned short console_linux_keysym(unsigned int table,
                                           unsigned int key)
{
    static const char plain_digits[] = "1234567890";
    static const char shift_digits[] = "!@#$%^&*()";

    if (key >= 2 && key <= 11)
        return (unsigned short)((table & 1U) ? shift_digits[key - 2]
                                             : plain_digits[key - 2]);
    if (key >= 16 && key <= 25)
        return (unsigned short)(((table & 1U) ? 'Q' : 'q') +
            ("qwertyuiop"[key - 16] - 'q'));
    if (key >= 30 && key <= 38)
        return (unsigned short)(((table & 1U) ? 'A' : 'a') +
            ("asdfghjkl"[key - 30] - 'a'));
    if (key >= 44 && key <= 50)
        return (unsigned short)(((table & 1U) ? 'Z' : 'z') +
            ("zxcvbnm"[key - 44] - 'z'));
    switch (key)
    {
    case 1: return 27;
    case 12: return (table & 1U) ? '_' : '-';
    case 13: return (table & 1U) ? '+' : '=';
    case 14: return 127;
    case 15: return '\t';
    case 26: return (table & 1U) ? '{' : '[';
    case 27: return (table & 1U) ? '}' : ']';
    case 28: return K_ENTER;
    case 29: return K_CTRL;
    case 39: return (table & 1U) ? ':' : ';';
    case 40: return (table & 1U) ? '"' : '\'';
    case 41: return (table & 1U) ? '~' : '`';
    case 42: return K_SHIFTL;
    case 43: return (table & 1U) ? '|' : '\\';
    case 51: return (table & 1U) ? '<' : ',';
    case 52: return (table & 1U) ? '>' : '.';
    case 53: return (table & 1U) ? '?' : '/';
    case 54: return K_SHIFTR;
    case 56: return K_ALT;
    case 57: return ' ';
    case 58: return K_CAPS;
    default:
        if (key >= 59 && key <= 68)
            return (unsigned short)K(KT_FN, key - 59);
        return 0;
    }
}

void devfs_set_read_nonblock(int nonblock)
{
    g_devfs_read_nonblock = nonblock ? 1 : 0;
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
    (void)entry;
    (void)offset;
    /* Honor O_NONBLOCK from the active sys_read (see devfs_set_read_nonblock). */
    return ir0_console_read(buf, count, g_devfs_read_nonblock);
}

int64_t dev_console_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
    struct ir0_termios termios;
    int ret;

    (void)entry;

    if (request == VT_OPENQRY)
    {
        int vtno = 1;

        if (!arg)
            return -EINVAL;
        if (copy_to_user(arg, &vtno, sizeof(vtno)) != 0)
            return -EFAULT;
        return 0;
    }

    if (request == VT_GETSTATE)
    {
        struct
        {
            uint16_t v_active;
            uint16_t v_signal;
            uint16_t v_state;
        } st;

        if (!arg)
            return -EINVAL;
        st.v_active = 1;
        st.v_signal = 0;
        st.v_state = (uint16_t)(1U << 1); /* VT1 occupied */
        if (copy_to_user(arg, &st, sizeof(st)) != 0)
            return -EFAULT;
        return 0;
    }

    if (request == VT_GETMODE)
    {
        if (!arg)
            return -EINVAL;
        if (copy_to_user(arg, &console_vt_mode, sizeof(console_vt_mode)) != 0)
            return -EFAULT;
        return 0;
    }

    if (request == VT_SETMODE)
    {
        struct vt_mode mode;

        if (!arg)
            return -EINVAL;
        if (copy_from_user(&mode, arg, sizeof(mode)) != 0)
            return -EFAULT;
        if (mode.mode != VT_AUTO && mode.mode != VT_PROCESS)
            return -EINVAL;
        console_vt_mode = mode;
        return 0;
    }

    if (request == VT_ACTIVATE || request == VT_WAITACTIVE)
        return (uintptr_t)arg == 1U ? 0 : -ENXIO;
    if (request == VT_DISALLOCATE || request == VT_RELDISP ||
        request == KDSETLED || request == KDMKTONE)
        return 0;

    if (request == KDGKBMODE)
    {
        int kbmode = console_vt_backend.get_keyboard_mode();

        if (!arg)
            return -EINVAL;
        if (copy_to_user(arg, &kbmode, sizeof(kbmode)) != 0)
            return -EFAULT;
        return 0;
    }

    if (request == KDSKBMODE)
        return console_vt_backend.set_keyboard_mode((int)(uintptr_t)arg);

    if (request == KDGKBENT)
    {
        struct kbentry ent;

        if (!arg)
            return -EINVAL;
        if (copy_from_user(&ent, arg, sizeof(ent)) != 0)
            return -EFAULT;
        ent.kb_value = console_linux_keysym(ent.kb_table, ent.kb_index);
        if (copy_to_user(arg, &ent, sizeof(ent)) != 0)
            return -EFAULT;
        return 0;
    }

    if (request == KDSETMODE)
    {
        int mode = (int)(uintptr_t)arg;

        if (mode != KD_TEXT && mode != KD_GRAPHICS)
            return -EINVAL;
        console_display_mode = mode;
        return 0;
    }

    if (request == KDGETMODE)
    {
        if (!arg)
            return -EINVAL;
        if (copy_to_user(arg, &console_display_mode,
                         sizeof(console_display_mode)) != 0)
            return -EFAULT;
        return 0;
    }

    if (request == IR0_CONSOLE_TCGETS)
    {
        if (!arg)
            return -EINVAL;
        ret = tty_ioctl_termios_kernel(IR0_CONSOLE_TCGETS, &termios);
        if (ret != 0)
            return ret;
        if (copy_to_user(arg, &termios, sizeof(termios)) != 0)
            return -EFAULT;
        return 0;
    }

    if (request == IR0_CONSOLE_TCSETS ||
        request == IR0_CONSOLE_TCSETSW ||
        request == IR0_CONSOLE_TCSETSF)
    {
        if (!arg)
            return -EINVAL;
        if (copy_from_user(&termios, arg, sizeof(termios)) != 0)
            return -EFAULT;
        return tty_ioctl_termios_kernel(request, &termios);
    }

    if (request == IR0_CONSOLE_TIOCGWINSZ)
        return ir0_console_ioctl_winsize(arg);
    if (request == IR0_CONSOLE_TIOCSWINSZ)
        return ir0_console_ioctl_winsize_set(arg);

    if (request == IR0_CONSOLE_TCFLSH)
    {
        /* 0=IFLUSH, 1=OFLUSH, 2=IOFLUSH — input-only console. */
        ir0_console_flush_input_session();
        return 0;
    }

    if (request == IR0_CONSOLE_FIONREAD)
    {
        int avail;

        if (!arg)
            return -EINVAL;
        avail = tty_input_bytes_available();
        if (copy_to_user(arg, &avail, sizeof(avail)) != 0)
            return -EFAULT;
        return 0;
    }

    /*
     * Job-control ioctls on /dev/console. Without these, BusyBox ash may
     * disable job control.
     */
    if (request == IR0_TIOCSCTTY)
	return ir0_console_ioctl_set_ctty();
    if (request == IR0_TIOCSPGRP)
    {
	pid_t pg;

	if (!arg)
	    return -EINVAL;
	if (copy_from_user(&pg, arg, sizeof(pg)) != 0)
	    return -EFAULT;
	return ir0_console_set_fg_pgid((int32_t)pg);
    }
    if (request == IR0_TIOCGPGRP)
    {
	pid_t pg;

	if (!arg)
	    return -EINVAL;
	pg = (pid_t)ir0_console_get_fg_pgid();
	if (copy_to_user(arg, &pg, sizeof(pg)) != 0)
	    return -EFAULT;
	return 0;
    }

    return -ENOTTY;
}

static void devfs_console_diag_once(devfs_entry_t *entry)
{
    static int diag_done;

    if (diag_done)
        return;
    diag_done = 1;

    klog_smoke("DEV_CONSOLE_NODE_OK");
    klog_info_fmt("DEVFS", "CONSOLE name=%s device_id=0x%x ops=console",
		  entry && entry->name ? entry->name : "(null)",
		  entry ? (unsigned)entry->device_id : 0u);
    klog_smoke("DEV_CONSOLE_OPEN_OK");
}

static int64_t dev_console_open(devfs_entry_t *entry, int flags)
{
    (void)flags;
    devfs_console_diag_once(entry);
    ir0_console_on_userspace_attach();
    return 0;
}

int64_t dev_console_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry;
    (void)offset;
    /* Linux graphical VT ownership: tty output must not overwrite X's fb. */
    if (console_display_mode == KD_GRAPHICS)
        return (int64_t)count;
    return ir0_console_write(buf, count, 0x07);
}

int dev_console_is_graphics(void)
{
    return console_display_mode == KD_GRAPHICS;
}

int64_t dev_kmsg_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    char message[97];
    size_t n;

    (void)entry;
    (void)offset;
    if (!buf)
        return -EINVAL;
    n = count < sizeof(message) - 1 ? count : sizeof(message) - 1;
    memcpy(message, buf, n);
    message[n] = '\0';
    while (n > 0 && (message[n - 1] == '\n' || message[n - 1] == '\r'))
        message[--n] = '\0';
    klog_info("USER", message);
    return (int64_t)count;
}

int64_t dev_kmsg_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
	/*
	 * Legacy ops path — preferred open path uses per-open text snap.
	 * Keep bounce+slice here so a direct ops->read still reaches EOF.
	 */
	char full[4096];
	int64_t n;

	(void)entry;
	n = (int64_t)klog_read_records(full, sizeof(full));
	if (n < 0)
		return n;
	if (offset < 0)
		return -EINVAL;
	if ((uint64_t)offset >= (uint64_t)n)
		return 0;
	{
		size_t avail = (size_t)n - (size_t)offset;
		size_t to_copy = avail < count ? avail : count;

		if (!buf)
			return -EFAULT;
		memcpy(buf, full + (size_t)offset, to_copy);
		return (int64_t)to_copy;
	}
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
    if (!audio_backend_is_available())
    {
        /* Sound Blaster not available, accept data but don't process */
        return (int64_t)count;
    }
    if (!buf || count == 0)
        return (int64_t)count;

    return (int64_t)audio_backend_play_pcm(buf, count,
                                           audio_pcm_format.sample_rate,
                                           audio_pcm_format.channels,
                                           audio_pcm_format.bits_per_sample);
#else
    (void)buf; (void)count;
    return -ENODEV;
#endif
}

int64_t dev_audio_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
    (void)entry;
#if CONFIG_ENABLE_SOUND
    if (!audio_backend_is_available())
    {
        return -1;  /* Device not available */
    }
    
    switch (request)
    {
        case AUDIO_SET_VOLUME:
            if (arg)
            {
                uint8_t volume;
                if (copy_from_user(&volume, arg, sizeof(volume)) != 0)
                    return -EFAULT;
                if (volume > 100)
                    volume = 100;  /* Clamp to 0-100 */
                /* Convert 0-100 to 0x00-0xFF mixer value */
                uint8_t mixer_vol = (volume * 255) / 100;
                audio_backend_set_master_volume(mixer_vol);
                return 0;
            }
            return -1;
            
        case AUDIO_GET_VOLUME:
            if (arg)
            {
                uint8_t mixer_vol = audio_backend_get_master_volume();
                /* Mixer value format: bits 7-4 = left, bits 3-0 = right */
                uint8_t left = (mixer_vol >> 4) & 0x0F;
                uint8_t right = mixer_vol & 0x0F;
                uint8_t avg = (left + right) / 2;
                uint8_t volume = (avg * 100) / 15;
                if (copy_to_user(arg, &volume, sizeof(volume)) != 0)
                    return -EFAULT;
                return 0;
            }
            return -1;
            
        case AUDIO_PLAY:
            audio_backend_speaker_on();
            return 0;
            
        case AUDIO_STOP:
            audio_backend_speaker_off();
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
    /*
     * Playback-only device: no capture backend exists. Returning 0 would
     * mean EOF to read(2) callers, so report the node as unsuitable for
     * reading instead (read(2) ERRORS: EINVAL).
     */
    return -EINVAL;
}

#if CONFIG_ENABLE_MOUSE
static int dev_mouse_can_read(devfs_entry_t *entry, pid_t pid)
{
    (void)entry;
    (void)pid;
	return input_mouse_packet_available() ? 1 : 0;
}
#endif

int64_t dev_mouse_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
#if CONFIG_ENABLE_MOUSE
    uint8_t packet[3];
    int n;
    static int read_path_logged;
    static uint8_t last_buttons;

    (void)entry;
    (void)offset;
    /* Linux KDrive consumes standard three-byte packets from /dev/mouse. */
    if (!buf || count < sizeof(packet))
        return 0;
    n = input_mouse_read_ps2(packet);
    if (n <= 0)
        return 0;
    memcpy(buf, packet, sizeof(packet));
    if ((packet[0] & 0x07u) != last_buttons)
    {
        last_buttons = packet[0] & 0x07u;
        klog_debug_fmt("INPUT", "DEV_MOUSE_BUTTONS mask=%u",
                       (unsigned)last_buttons);
    }
    if (!read_path_logged)
    {
        read_path_logged = 1;
        klog_debug_fmt("INPUT", "DEV_MOUSE_READ_PATH_OK");
    }
    return (int64_t)sizeof(packet);
#else
    (void)entry; (void)buf; (void)count; (void)offset;
    return -ENODEV;
#endif
}

static int64_t dev_mouse_write(devfs_entry_t *entry, const void *buf,
                               size_t count, off_t offset)
{
#if CONFIG_ENABLE_MOUSE
    static int first_write_logged;

    (void)entry;
    (void)offset;

    if (!input_mouse_is_available())
        return -ENODEV;
    if (!buf && count != 0)
        return -EFAULT;

    if (!first_write_logged)
    {
        first_write_logged = 1;
        klog_debug_fmt("INPUT", "DEV_MOUSE_WRITE bytes=%u", (unsigned)count);
    }

    /*
     * Linux KDrive probes a legacy psaux-style node by writing PS/2
     * initialization commands before consuming packets. IR0 already owns
     * and configures the physical i8042 device in-kernel; forwarding those
     * bytes would race its IRQ packet parser with ACK/ID replies. Accepting
     * the complete command stream is the compatibility contract here: the
     * requested operational state (enabled stream, negotiated packet size)
     * is already maintained by the driver.
     */
    return (int64_t)count;
#else
    (void)entry;
    (void)buf;
    (void)count;
    (void)offset;
    return -ENODEV;
#endif
}

int64_t dev_mouse_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
#if CONFIG_ENABLE_MOUSE
    (void)entry;
    
    if (!input_mouse_is_available())
    {
        return -1;  /* Device not available */
    }
    
    switch (request)
    {
        case MOUSE_GET_STATE:
            if (arg)
            {
                ir0_mouse_state_t st;
                if (input_mouse_get_state(&st))
                {
                    if (copy_to_user(arg, &st, sizeof(st)) != 0)
                        return -EFAULT;
                    return 0;
                }
            }
            return -1;
            
        case MOUSE_SET_SENSITIVITY:
            if (arg)
            {
                uint8_t sensitivity;
                if (copy_from_user(&sensitivity, arg,
                                   sizeof(sensitivity)) != 0)
                    return -EFAULT;
                /* Sensitivity maps to sample rate: higher = more sensitive */
                /* Typical range: 10-200 samples/sec, default 100 */
                if (sensitivity < 10)
                    sensitivity = 10;
                if (sensitivity > 200)
                    sensitivity = 200;
                if (input_mouse_set_sensitivity(sensitivity))
                {
                    return 0;
                }
            }
            return -1;
            
        default:
            return -1;  /* Invalid request */
    }
#else
    (void)entry; (void)request; (void)arg;
    return -ENODEV;
#endif
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
    .ioctl = dev_console_ioctl,
    .open = dev_console_open,
    .can_read = devfs_console_can_read,
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
    .write = dev_mouse_write,
    .ioctl = dev_mouse_ioctl,
#if CONFIG_ENABLE_MOUSE
    .can_read = dev_mouse_can_read,
#endif
};

extern devfs_node_t dev_mouse;

static void dev_mouse_ready_notify(void)
{
    io_async_notify_device(dev_mouse.entry.device_id);
}

static const devfs_ops_t net_ops = {
    .read = dev_net_read,
    .write = dev_net_write,
    .ioctl = dev_net_ioctl,
#if CONFIG_ENABLE_NETWORKING
    .can_read = devfs_net_can_read,
#endif
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

static int64_t dev_fb0_open(devfs_entry_t *entry, int flags)
{
    (void)entry;
    (void)flags;
    if (!ir0_fb_is_available())
        return -ENODEV;
    return 0;
}

static int64_t dev_fb0_write_simple(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry;
    return ir0_fb_write_bytes((size_t)offset, buf, count);
}

static int64_t dev_fb0_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
    struct ir0_fb_info info;

    (void)entry;
    if (!ir0_fb_get_info(&info))
        return -ENODEV;
    if (request == FBIOGET_VSCREENINFO && arg)
    {
        struct fb_var_screeninfo var_info;

        memset(&var_info, 0, sizeof(var_info));
        var_info.xres = info.width;
        var_info.yres = info.height;
        var_info.xres_virtual = info.width;
        var_info.yres_virtual = info.height;
        var_info.bits_per_pixel = info.bpp;
        /*
         * Default matches vbe_rgb_to_pixel() BGR888 + alpha in high byte
         * (common QEMU/PC framebuffer layout).
         */
        if (info.bpp == 32 || info.bpp == 24)
        {
            var_info.red.offset = 16;
            var_info.red.length = 8;
            var_info.green.offset = 8;
            var_info.green.length = 8;
            var_info.blue.offset = 0;
            var_info.blue.length = 8;
            if (info.bpp == 32)
            {
                var_info.transp.offset = 24;
                var_info.transp.length = 8;
            }
        }
        else if (info.bpp == 16)
        {
            var_info.red.offset = 11;
            var_info.red.length = 5;
            var_info.green.offset = 5;
            var_info.green.length = 6;
            var_info.blue.offset = 0;
            var_info.blue.length = 5;
        }
        if (copy_to_user(arg, &var_info, sizeof(var_info)) != 0)
            return -EFAULT;
        return 0;
    }
    /*
     * Fixed VBE/QEMU framebuffer: no mode switching. Accept PUT so TinyX
     * KdFindMode probes do not abort; subsequent FBIOGET_* report hardware.
     */
    if (request == FBIOPUT_VSCREENINFO && arg)
    {
        struct fb_var_screeninfo var;

        if (copy_from_user(&var, arg, sizeof(var)) != 0)
            return -EFAULT;
        (void)var;
        (void)info;
        return 0;
    }
    if (request == FBIOGET_FSCREENINFO && arg)
    {
        struct fb_fix_screeninfo fix;

        memset(&fix, 0, sizeof(fix));
        strncpy(fix.id, "IR0 FB", sizeof(fix.id) - 1);
        /*
         * IR0 mmap(/dev/fb0) maps fb_phys at offset 0 of the returned VA
         * (see sys_mmap fb path). TinyX then does
         *   fb = mmap_base + (smem_start % pagesize).
         * Reporting the raw physical address when it is not page-aligned
         * makes TinyX skip the first bytes and write past the mapping.
         * Use 0 so the userspace offset is always 0.
         */
        fix.smem_start = 0;
        fix.smem_len = info.fb_size;
        fix.type = FB_TYPE_PACKED_PIXELS;
        fix.visual = FB_VISUAL_TRUECOLOR;
        fix.line_length = info.pitch;
        fix.accel = FB_ACCEL_NONE;
        /* filled on VGET; here only fix */
        if (copy_to_user(arg, &fix, sizeof(fix)) != 0)
            return -EFAULT;
        return 0;
    }
    /* No hardware pan/blank — succeed so KDrive enable path continues. */
    if (request == FBIOPAN_DISPLAY || request == FBIOBLANK)
        return 0;
    /*
     * Colormap put/get: TrueColor FB ignores palette; accept so TinyX
     * fbdevUpdateFbColormap does not abort enable.
     */
    if (request == FBIOPUTCMAP || request == FBIOGETCMAP)
        return 0;
    return -EINVAL;
}

static const devfs_ops_t fb0_ops = {
    .read = dev_fb0_read,
    .write = dev_fb0_write_simple,
    .ioctl = dev_fb0_ioctl,
    .open = dev_fb0_open,
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
    struct input_event ev_buf[16];
    size_t max_ev;
    size_t n;
    size_t i;

    (void)entry;
    (void)offset;
    if (count < sizeof(struct input_event))
        return 0;

    max_ev = count / sizeof(struct input_event);
    if (max_ev > 16)
        max_ev = 16;

    n = 0;
    for (i = 0; i < max_ev; i++)
    {
        struct ir0_input_event ir0_ev;
        int rc = ir0_input_read_event(&ir0_ev);

        if (rc <= 0)
            break;
        ev_buf[i].time.tv_sec = (time_t)(ir0_ev.timestamp_ms / 1000);
        ev_buf[i].time.tv_usec = (suseconds_t)((ir0_ev.timestamp_ms % 1000) * 1000);
        ev_buf[i].type = ir0_ev.type;
        ev_buf[i].code = ir0_ev.code;
        ev_buf[i].value = ir0_ev.value;
        n++;
    }

    if (n == 0)
        return 0;

    {
        size_t bytes = n * sizeof(struct input_event);
        memcpy(buf, ev_buf, bytes);
        return (int64_t)bytes;
    }
}

static void evdev_set_bit(unsigned long *bits, size_t nlongs, unsigned int bit)
{
    unsigned int idx = bit / (8u * sizeof(unsigned long));
    unsigned int off = bit % (8u * sizeof(unsigned long));

    if (idx >= nlongs)
        return;
    bits[idx] |= (1UL << off);
}

static int64_t dev_events0_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
    struct ir0_input_caps caps;
    unsigned dir;
    unsigned type;
    unsigned nr;
    unsigned size;

    (void)entry;

    if (request == IR0_INPUT_IOCTL_GET_CAPS)
    {
        if (!arg)
            return -EFAULT;
        ir0_input_get_caps(&caps);
        if (copy_to_user(arg, &caps, sizeof(caps)) != 0)
            return -EFAULT;
        return 0;
    }

    if (request == IR0_INPUT_IOCTL_INJECT)
    {
#if CONFIG_TEST_INPUT_INJECT
        struct ir0_input_event ev;

        if (!arg)
            return -EFAULT;
        if (copy_from_user(&ev, arg, sizeof(ev)) != 0)
            return -EFAULT;
        {
            int rc = ir0_input_inject_event(ev.type, ev.code, ev.value);

            if (rc < 0)
                return rc;
            /* Linux clients expect a SYN_REPORT marker after injected edges. */
            if (ev.type != EV_SYN)
                (void)ir0_input_inject_event(EV_SYN, SYN_REPORT, 0);
            return rc;
        }
#else
        (void)arg;
        return -ENOTTY;
#endif
    }

    dir = (unsigned)((request >> EVIOC_DIR_SHIFT) & 0x3u);
    type = (unsigned)((request >> EVIOC_TYPE_SHIFT) & 0xffu);
    nr = (unsigned)(request & EVIOC_NR_MASK);
    size = (unsigned)((request >> EVIOC_SIZE_SHIFT) & EVIOC_SIZE_MASK);

    if (type == (unsigned)'E' && dir == EVIOC_READ && nr == 0x01u)
    {
        int version = EV_VERSION;

        if (!arg || size < sizeof(int))
            return -EINVAL;
        if (copy_to_user(arg, &version, sizeof(version)) != 0)
            return -EFAULT;
        return 0;
    }

    if (type == (unsigned)'E' && dir == EVIOC_READ && nr >= EVIOCGBIT_BASE &&
        nr < EVIOCGBIT_BASE + EV_CNT)
    {
        unsigned long bits[8];
        unsigned ev = nr - EVIOCGBIT_BASE;
        size_t nlongs;
        size_t copy_n;

        if (!arg || size == 0)
            return -EINVAL;
        memset(bits, 0, sizeof(bits));
        nlongs = sizeof(bits) / sizeof(bits[0]);

        if (ev == 0)
        {
            /* Bitmap of supported event types. */
            evdev_set_bit(bits, nlongs, EV_SYN);
            evdev_set_bit(bits, nlongs, EV_KEY);
            evdev_set_bit(bits, nlongs, EV_REL);
        }
        else if (ev == EV_KEY)
        {
            unsigned k;

            for (k = 1; k < (unsigned)KEY_MAX && k < 256u; k++)
                evdev_set_bit(bits, nlongs, k);
            evdev_set_bit(bits, nlongs, BTN_LEFT);
            evdev_set_bit(bits, nlongs, BTN_RIGHT);
            evdev_set_bit(bits, nlongs, BTN_MIDDLE);
            evdev_set_bit(bits, nlongs, BTN_SIDE);
            evdev_set_bit(bits, nlongs, BTN_EXTRA);
        }
        else if (ev == EV_REL)
        {
            evdev_set_bit(bits, nlongs, REL_X);
            evdev_set_bit(bits, nlongs, REL_Y);
            evdev_set_bit(bits, nlongs, REL_WHEEL);
        }
        else if (ev == EV_SYN)
        {
            evdev_set_bit(bits, nlongs, SYN_REPORT);
        }
        /* other EV_* → empty bitmap (supported) */

        copy_n = size;
        if (copy_n > sizeof(bits))
            copy_n = sizeof(bits);
        if (copy_to_user(arg, bits, copy_n) != 0)
            return -EFAULT;
        return 0;
    }

    return -EINVAL;
}

static int64_t dev_events0_open(devfs_entry_t *entry, int flags)
{
	(void)entry;
	(void)flags;
	input_events_reader_open();
	return 0;
}

static int64_t dev_events0_close(devfs_entry_t *entry)
{
	(void)entry;
	input_events_reader_close();
	return 0;
}

static const devfs_ops_t events0_ops = {
    .read = dev_events0_read,
    .write = NULL,
    .ioctl = dev_events0_ioctl,
    .open = dev_events0_open,
    .close = dev_events0_close,
    .can_read = devfs_events0_can_read,
    .can_write = devfs_events0_can_write,
};

static devfs_node_t dev_events0 = {
    .entry = { .name = "events0", .mode = 0660, .device_id = 16 },
    .ops = &events0_ops,
    .ref_count = 0,
};

/* Linux path alias for Xfbdev (-mouse evdev) */
static devfs_node_t dev_input_event0 = {
    .entry = { .name = "input/event0", .mode = 0660, .device_id = 45 },
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
    .can_read = devfs_serial_can_read,
    .can_write = devfs_serial_can_write,
};

static devfs_node_t dev_serial = {
    .entry = { .name = "serial", .mode = 0220, .device_id = 42 },
    .ops = &serial_ops,
    .ref_count = 0,
};


/*
 * /dev/tty is the controlling terminal of the calling session, not an alias
 * for /dev/console.  Linux resolves it dynamically (drivers/tty/tty_io.c);
 * do the same so unmodified readpassphrase/sudo/OpenDoas reach their PTY.
 */
static struct pty_pair *dev_tty_current_pty(int *slot_out)
{
	int i;

	if (!current_process)
		return NULL;
	for (i = 0; i < DEVFS_PTY_MAX; i++)
	{
		if (g_ptys[i].ctty_set &&
		    g_ptys[i].session_sid == current_process->sid)
		{
			if (slot_out)
				*slot_out = i;
			return &g_ptys[i];
		}
	}
	return NULL;
}

static int dev_tty_uses_console(void)
{
	return current_process &&
	       ir0_console_has_ctty_for_sid((int32_t)current_process->sid);
}

static int64_t dev_tty_read(devfs_entry_t *entry, void *buf, size_t count,
			    off_t offset)
{
	struct pty_pair *p = dev_tty_current_pty(NULL);

	if (p)
		return pty_ring_pop(&p->m2s, (char *)buf, count);
	if (dev_tty_uses_console())
		return dev_console_read(entry, buf, count, offset);
	return -ENXIO;
}

static int64_t dev_tty_write(devfs_entry_t *entry, const void *buf,
			     size_t count, off_t offset)
{
	int slot = -1;

	if (dev_tty_current_pty(&slot))
		return dev_pts_write(&dev_pts_nodes[slot].entry, buf, count, offset);
	if (dev_tty_uses_console())
		return dev_console_write(entry, buf, count, offset);
	return -ENXIO;
}

static int64_t dev_tty_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
	struct pty_pair *p;
	int slot = -1;

	p = dev_tty_current_pty(&slot);
	if (p)
		return pty_master_ioctl_pair(p, slot, request, arg);
	if (dev_tty_uses_console())
		return dev_console_ioctl(entry, request, arg);
	return -ENXIO;
}

static int64_t dev_tty_open(devfs_entry_t *entry, int flags)
{
	(void)entry;
	(void)flags;
	return dev_tty_current_pty(NULL) || dev_tty_uses_console() ? 0 : -ENXIO;
}

static int dev_tty_can_read(devfs_entry_t *entry, pid_t pid)
{
	struct pty_pair *p = dev_tty_current_pty(NULL);

	(void)entry;
	(void)pid;
	if (p)
		return (p->m2s.count > 0 || !p->master_open) ? 1 : 0;
	return dev_tty_uses_console() ? devfs_console_can_read(entry, pid) : 0;
}

static int dev_tty_can_write(devfs_entry_t *entry, pid_t pid)
{
	struct pty_pair *p = dev_tty_current_pty(NULL);

	(void)entry;
	(void)pid;
	if (p)
		return p->s2m.count < PTY_BUF_SIZE ? 1 : 0;
	return dev_tty_uses_console() ? 1 : 0;
}

static const devfs_ops_t tty_ops = {
	.read = dev_tty_read,
	.write = dev_tty_write,
	.ioctl = dev_tty_ioctl,
	.open = dev_tty_open,
	.can_read = dev_tty_can_read,
	.can_write = dev_tty_can_write,
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
    return ir0_bt_hci_read((char *)buf, count);
}

int64_t dev_bluetooth_hci_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry; (void)offset;
    return ir0_bt_hci_write((const char *)buf, count);
}

int64_t dev_bluetooth_hci_open(devfs_entry_t *entry, int flags)
{
    (void)entry; (void)flags;
    return ir0_bt_hci_open();
}

int64_t dev_bluetooth_hci_close(devfs_entry_t *entry)
{
    (void)entry;
    return ir0_bt_hci_close();
}

int64_t dev_bluetooth_hci_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
    (void)entry;
    return ir0_bt_hci_ioctl((unsigned int)request, (unsigned long)arg);
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
    .entry = { .name = "null", .mode = 0666, .device_id = 1, .rdev = IR0_MKDEV(1, 3) },
    .ops = &null_ops,
    .ref_count = 0
};

devfs_node_t dev_zero = {
    .entry = { .name = "zero", .mode = 0666, .device_id = 2, .rdev = IR0_MKDEV(1, 5) },
    .ops = &zero_ops,
    .ref_count = 0
};

devfs_node_t dev_console = {
    .entry = { .name = "console", .mode = 0620, .device_id = 3, .rdev = IR0_MKDEV(5, 1) },
    .ops = &console_ops,
    .ref_count = 0
};

devfs_node_t dev_tty = {
    .entry = { .name = "tty", .mode = 0620, .device_id = 4, .rdev = IR0_MKDEV(5, 0) },
    .ops = &tty_ops,
    .ref_count = 0
};

/* Synthetic VTs for TinyX/Xfbdev LinuxInit (VT_OPENQRY → tty1). */
devfs_node_t dev_tty0 = {
    .entry = { .name = "tty0", .mode = 0620, .device_id = 46 },
    .ops = &console_ops,
    .ref_count = 0
};

devfs_node_t dev_tty1 = {
    .entry = { .name = "tty1", .mode = 0620, .device_id = 47 },
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

/*
 * Registers /dev/hda..hdc only when a disk is present, and hdX{N} partitions
 * only when the partition table exposes them (never a fixed hdX4 grid).
 */



devfs_node_t dev_random = {
    .entry = { .name = "random", .mode = 0644, .device_id = 10, .rdev = IR0_MKDEV(1, 8) },
    .ops = &random_ops,
    .ref_count = 0
};

devfs_node_t dev_urandom = {
    .entry = { .name = "urandom", .mode = 0644, .device_id = 11, .rdev = IR0_MKDEV(1, 9) },
    .ops = &urandom_ops,
    .ref_count = 0
};

devfs_node_t dev_full = {
    .entry = { .name = "full", .mode = 0666, .device_id = 12, .rdev = IR0_MKDEV(1, 7) },
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
    if (devfs_find_node_by_id(node->entry.device_id))
    {
        klog_print("DEVFS_DUP_DEVICE_ID\n");
        return -EEXIST;
    }
    dev_nodes[num_dev_nodes++] = node;
    return 0;
}

static int devfs_register_node_checked(devfs_node_t *node)
{
    int rc = devfs_register_node(node);

    if (rc != 0)
        klog_print("DEVFS_INIT_REGISTER_FAIL\n");
    return rc;
}

int devfs_init(void)
{
    if (devfs_register_node_checked(&dev_null) != 0)
        return -1;
    if (devfs_register_node_checked(&dev_zero) != 0)
        return -1;
    if (devfs_register_node_checked(&dev_console) != 0)
        return -1;
    if (devfs_register_node_checked(&dev_tty) != 0)
        return -1;
    if (devfs_register_node_checked(&dev_tty0) != 0)
        return -1;
    if (devfs_register_node_checked(&dev_tty1) != 0)
        return -1;
    if (devfs_register_node_checked(&dev_stdin) != 0)
        return -1;
    if (devfs_register_node_checked(&dev_stdout) != 0)
        return -1;
    if (devfs_register_node_checked(&dev_stderr) != 0)
        return -1;
    if (devfs_register_node_checked(&dev_kmsg) != 0)
        return -1;
    if (devfs_register_node_checked(&dev_audio) != 0)
        return -1;
    if (devfs_register_node_checked(&dev_mouse) != 0)
        return -1;
    input_mouse_set_ready_notifier(dev_mouse_ready_notify);
    if (devfs_register_node_checked(&dev_net) != 0)
        return -1;
    if (devfs_register_node_checked(&dev_disk) != 0)
        return -1;
    devfs_register_disk_topology();
    if (devfs_register_node_checked(&dev_random) != 0)
        return -1;
    if (devfs_register_node_checked(&dev_urandom) != 0)
        return -1;
    if (devfs_register_node_checked(&dev_full) != 0)
        return -1;
    if (devfs_register_node_checked(&dev_fb0) != 0)
        return -1;
    if (devfs_register_node_checked(&dev_events0) != 0)
        return -1;
    if (devfs_register_node_checked(&dev_input_event0) != 0)
        return -1;
    if (devfs_register_node_checked(&dev_serial) != 0)
        return -1;
    if (devfs_register_node_checked(&dev_ptmx) != 0)
        return -1;
    devfs_pty_register_nodes();
    if (devfs_register_node_checked(&dev_ipc) != 0)
        return -1;
#if CONFIG_ENABLE_BLUETOOTH
    if (devfs_register_node_checked(&dev_bluetooth_hci0) != 0)
        return -1;
#endif
    ktm_userdev_register();

    return 0;
}

int64_t devfs_open_node(devfs_node_t *node, int flags)
{
    int64_t rc;

    if (!node)
        return -EINVAL;

    if (node->ops && node->ops->open)
    {
        rc = node->ops->open(&node->entry, flags);
        if (rc < 0)
            return rc;
    }

    {
        unsigned long irq_flags = irq_save();

        node->ref_count++;
        irq_restore(irq_flags);
    }
    return 0;
}

int64_t devfs_close_node(devfs_node_t *node)
{
    int64_t rc;
    unsigned long irq_flags;
    int ref_after;

    if (!node)
        return -EINVAL;

    irq_flags = irq_save();
    if (node->ref_count == 0)
    {
        irq_restore(irq_flags);
        return -EBADF;
    }

    node->ref_count--;
    ref_after = node->ref_count;
    irq_restore(irq_flags);

    rc = 0;
    if (node->ops && node->ops->close)
    {
        /*
         * events0: per-fd release (reader divert count).
         * Other devices: last-close teardown only.
         */
        if (node == &dev_events0 || node == &dev_input_event0 ||
            ref_after == 0)
            rc = node->ops->close(&node->entry);
    }

    return rc;
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

int devfs_stat_path(const char *path, stat_t *buf)
{
    devfs_node_t *node;

    if (!path || !buf)
        return -EINVAL;

    if (strcmp(path, "/dev") == 0 || strcmp(path, "/dev/") == 0)
    {
        memset(buf, 0, sizeof(*buf));
        buf->st_mode = S_IFDIR | 0755;
        buf->st_nlink = 2;
        buf->st_uid = 0;
        buf->st_gid = 0;
        buf->st_blksize = 512;
        return 0;
    }

    if (devfs_is_virtual_subdir(path))
    {
        memset(buf, 0, sizeof(*buf));
        buf->st_mode = S_IFDIR | 0755;
        buf->st_nlink = 2;
        buf->st_uid = 0;
        buf->st_gid = 0;
        buf->st_blksize = 512;
        return 0;
    }

    node = devfs_find_node(path);
    if (!node)
        return -ENOENT;

    memset(buf, 0, sizeof(*buf));
    buf->st_mode = S_IFCHR | (node->entry.mode & 0777);
    buf->st_rdev = node->entry.device_id;
    buf->st_nlink = 1;
    buf->st_uid = node->entry.uid;
    buf->st_gid = node->entry.gid;
    buf->st_blksize = 512;
    return 0;
}

int devfs_chown_path(const char *path, uid_t owner, gid_t group)
{
    devfs_node_t *node;

    if (!path)
        return -EINVAL;
    node = devfs_find_node(path);
    if (!node)
        return -ENOENT;
    if (owner != (uid_t)-1)
        node->entry.uid = owner;
    if (group != (gid_t)-1)
        node->entry.gid = group;
    return 0;
}

static const char *devfs_virtual_subdir_prefix(const char *path)
{
    if (!path)
        return NULL;

    if (strcmp(path, "/dev/pts") == 0 || strcmp(path, "/dev/pts/") == 0)
        return "pts";
    if (strcmp(path, "/dev/input") == 0 || strcmp(path, "/dev/input/") == 0)
        return "input";
    if (strcmp(path, "/dev/bluetooth") == 0 ||
        strcmp(path, "/dev/bluetooth/") == 0)
        return "bluetooth";

    return NULL;
}

int devfs_is_virtual_subdir(const char *path)
{
    return devfs_virtual_subdir_prefix(path) != NULL;
}

int devfs_readdir_subdir(const char *path, struct vfs_dirent *entries,
                         int max_entries)
{
    const char *prefix;
    size_t prefix_len;
    int n = 0;
    int i;
    int j;

    prefix = devfs_virtual_subdir_prefix(path);
    if (!prefix || !entries || max_entries <= 0)
        return -EINVAL;

    prefix_len = strlen(prefix);
    memset(entries, 0, (size_t)max_entries * sizeof(struct vfs_dirent));

    strncpy(entries[n].name, ".", sizeof(entries[n].name) - 1);
    entries[n].type = DT_DIR;
    n++;
    if (n >= max_entries)
        return n;

    strncpy(entries[n].name, "..", sizeof(entries[n].name) - 1);
    entries[n].type = DT_DIR;
    n++;

    for (i = 0; i < num_dev_nodes && n < max_entries; i++)
    {
        const char *name;
        const char *leaf;
        const char *slash;
        int exists = 0;

        if (!dev_nodes[i] || !dev_nodes[i]->entry.name)
            continue;

        name = dev_nodes[i]->entry.name;
        if (strncmp(name, prefix, prefix_len) != 0 || name[prefix_len] != '/')
            continue;

        leaf = name + prefix_len + 1;
        slash = strchr(leaf, '/');
        if (slash)
            continue;

        for (j = 0; j < n; j++)
        {
            if (strcmp(entries[j].name, leaf) == 0)
            {
                exists = 1;
                break;
            }
        }
        if (exists)
            continue;

        strncpy(entries[n].name, leaf, sizeof(entries[n].name) - 1);
        entries[n].type = DT_UNKNOWN;
        n++;
    }

    return n;
}

int devfs_readdir_root(struct vfs_dirent *entries, int max_entries)
{
    int n = 0;
    int i;
    int j;

    if (!entries || max_entries <= 0)
        return -EINVAL;

    memset(entries, 0, (size_t)max_entries * sizeof(struct vfs_dirent));

    strncpy(entries[n].name, ".", sizeof(entries[n].name) - 1);
    entries[n].type = DT_DIR;
    n++;
    if (n >= max_entries)
        return n;

    strncpy(entries[n].name, "..", sizeof(entries[n].name) - 1);
    entries[n].type = DT_DIR;
    n++;

    for (i = 0; i < num_dev_nodes && n < max_entries; i++)
    {
        const char *name;
        char top[VFS_PATH_MAX];
        const char *slash;
        size_t len;
        int exists = 0;

        if (!dev_nodes[i] || !dev_nodes[i]->entry.name)
            continue;

        name = dev_nodes[i]->entry.name;
        slash = strchr(name, '/');
        len = slash ? (size_t)(slash - name) : strlen(name);
        if (len == 0 || len >= sizeof(top))
            continue;

        memcpy(top, name, len);
        top[len] = '\0';

        for (j = 0; j < n; j++)
        {
            if (strcmp(entries[j].name, top) == 0)
            {
                exists = 1;
                break;
            }
        }
        if (exists)
            continue;

        strncpy(entries[n].name, top, sizeof(entries[n].name) - 1);
        entries[n].type = slash ? DT_DIR : DT_UNKNOWN;
        n++;
    }

    return n;
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

devfs_node_t *devfs_find_node_by_rdev(uint32_t rdev)
{
    if (rdev == 0)
        return NULL;

    for (int i = 0; i < num_dev_nodes; i++)
    {
        if (dev_nodes[i] && dev_nodes[i]->entry.rdev == rdev)
            return dev_nodes[i];
    }
    return NULL;
}

int devfs_register_device(const char *name, const devfs_ops_t *ops, uint32_t mode)
{
    if (!name || !ops)
        return -1;

    if (num_dev_nodes >= MAX_DEV_NODES)
        return -1;
    
    devfs_node_t *node = kmalloc_try(sizeof(devfs_node_t));
    if (!node)
        return -1;

    size_t name_len = strlen(name);
    char *name_copy = kmalloc_try(name_len + 1);
    if (!name_copy)
    {
        kfree(node);
        return -1;
    }
    memcpy(name_copy, name, name_len + 1);
    
    node->entry.name = name_copy;
    node->entry.mode = mode;
    node->entry.device_id = num_dev_nodes + 100;  /* Dynamic IDs */
    node->ops = ops;
    node->ref_count = 0;
    
    dev_nodes[num_dev_nodes++] = node;
    return 0;
}

int devfs_unregister_device(const char *name)
{
    if (!name)
        return -1;

    for (int i = 0; i < num_dev_nodes; i++)
    {
        devfs_node_t *node = dev_nodes[i];
        if (!node || !node->entry.name)
            continue;
        if (strcmp(node->entry.name, name) != 0)
            continue;

        /*
         * Dynamic devices registered via devfs_register_device use IDs >= 100.
         * Built-in static nodes are not owned by this unregistration path.
         */
        if (node->entry.device_id < 100)
            return -1;

        if (node->entry.name)
            kfree((void *)node->entry.name);
        kfree(node);

        for (int j = i; j < num_dev_nodes - 1; j++)
            dev_nodes[j] = dev_nodes[j + 1];
        dev_nodes[num_dev_nodes - 1] = NULL;
        num_dev_nodes--;
        return 0;
    }

    return -1;
}
