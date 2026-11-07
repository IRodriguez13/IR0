/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: devfs_console.c
 * Description: console, virtual terminal and kernel log nodes
 */

#include "devfs_internal.h"

int devfs_console_can_read(devfs_entry_t *entry, pid_t pid)
{
    (void)entry;
    (void)pid;
    return ir0_console_poll();
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
devfs_node_t dev_console = {
    .entry = { .name = "console", .mode = 0620, .device_id = 3, .rdev = IR0_MKDEV(5, 1) },
    .ops = &console_ops,
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

