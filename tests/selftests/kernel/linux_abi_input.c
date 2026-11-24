/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: linux_abi_input.c
 * Description: The input class against Linux drivers/input/input.c,
 *              evdev.c and misc/uinput.c: one /dev/input/eventN per device
 *              (the i8042 keyboard and mouse apart), a ring per open with
 *              SYN_DROPPED on overflow, whole packets only, grab, revoke,
 *              per-client clocks, the EVIOCG* lengths and errnos, write()
 *              injection and uinput devices. Events come from a uinput
 *              device made of buttons and REL_X only, so no console
 *              keyboard or mouse handler sees them; without /dev/uinput a
 *              keyboard node is used. Each case passes on Linux with
 *              the runner as init (make ir0-selftest-input-abi-{x86,arm64}).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "conf.h"

#include <time.h>

/* asm-generic _IOC() encoding: the same on x86_64 and arm64. */
#define T_IOC(dir, type, nr, size) \
	(((unsigned long)(dir) << 30) | ((unsigned long)(size) << 16) | \
	 ((unsigned long)(type) << 8) | (unsigned long)(nr))
#define T_IO(type, nr) T_IOC(0, type, nr, 0)
#define T_IOR(type, nr, size) T_IOC(2, type, nr, size)
#define T_IOW(type, nr, size) T_IOC(1, type, nr, size)

#define T_EVIOCGVERSION T_IOR('E', 0x01, 4)
#define T_EVIOCGID T_IOR('E', 0x02, 8)
#define T_EVIOCGNAME(len) T_IOR('E', 0x06, len)
#define T_EVIOCGPHYS(len) T_IOR('E', 0x07, len)
#define T_EVIOCGPROP(len) T_IOR('E', 0x09, len)
#define T_EVIOCGKEY(len) T_IOR('E', 0x18, len)
#define T_EVIOCGBIT(ev, len) T_IOR('E', 0x20 + (ev), len)
#define T_EVIOCGABS(abs) T_IOR('E', 0x40 + (abs), 24)
#define T_EVIOCGRAB T_IOW('E', 0x90, 4)
#define T_EVIOCREVOKE T_IOW('E', 0x91, 4)
#define T_EVIOCSCLOCKID T_IOW('E', 0xa0, 4)

#define T_UI_DEV_CREATE T_IO('U', 1)
#define T_UI_DEV_DESTROY T_IO('U', 2)
#define T_UI_DEV_SETUP T_IOW('U', 3, 92)
#define T_UI_SET_EVBIT T_IOW('U', 100, 4)
#define T_UI_SET_KEYBIT T_IOW('U', 101, 4)
#define T_UI_SET_RELBIT T_IOW('U', 102, 4)
#define T_UI_GET_VERSION T_IOR('U', 45, 4)

#define T_TCGETS 0x5401

#define T_EV_SYN 0x00
#define T_EV_KEY 0x01
#define T_EV_REL 0x02
#define T_EV_REP 0x14
#define T_SYN_REPORT 0
#define T_SYN_DROPPED 3
#define T_KEY_A 30
#define T_BTN_0 0x100
#define T_BTN_LEFT 0x110
#define T_REL_X 0x00
#define T_REL_Y 0x01
#define T_ABS_X 0x00
#define T_BUS_I8042 0x11
#define T_BUS_VIRTUAL 0x06
#define T_INPUT_PROP_POINTER 0

#define T_POLLIN 0x0001

/* 64 * KEY_MAX bits, and the 64-bit long of EV_MAX bits. */
#define T_KEY_LEN 96
#define T_EV_LEN 8

#define T_DEV_NAME "ir0 input abi"

struct t_input_event
{
	long sec;
	long usec;
	unsigned short type;
	unsigned short code;
	int value;
};

struct t_input_id
{
	unsigned short bustype;
	unsigned short vendor;
	unsigned short product;
	unsigned short version;
};

struct t_uinput_setup
{
	struct t_input_id id;
	char name[80];
	unsigned int ff_effects_max;
};

struct t_pollfd
{
	int fd;
	short events;
	short revents;
};

struct t_ts
{
	long sec;
	long nsec;
};

/* The device under test: uinput when the kernel has it, else i8042. */
static long g_ufd = -1;
static unsigned int g_key = T_BTN_0;

static int bit_set(const unsigned char *bits, unsigned int nr)
{
	return (bits[nr / 8] >> (nr % 8)) & 1;
}

static void dev_mount(void)
{
	long fd = S4(SYS_openat, AT_FDCWD, "/dev/input", O_RDONLY | O_DIRECTORY, 0);

	/* Linux as init: /dev is an empty initramfs directory. */
	if (fd < 0)
		(void)S5(SYS_mount, "devtmpfs", "/dev", "devtmpfs", 0, 0);
	else
		(void)S1(SYS_close, fd);
}

static long open_event(int n, int flags)
{
	char path[32];

	path[0] = '\0';
	rt_cat(path, sizeof(path), "/dev/input/event");
	rt_catn(path, sizeof(path), n);
	return S4(SYS_openat, AT_FDCWD, path, flags, 0);
}

/* The event node whose EVIOCGNAME is @name, opened with @flags; -ENOENT. */
static long find_by_name(const char *name, int flags)
{
	char got[96];
	int n;

	for (n = 0; n < 32; n++)
	{
		long fd = open_event(n, flags);

		if (fd < 0)
			continue;
		rt_fill(got, 0, sizeof(got));
		if (S3(SYS_ioctl, fd, T_EVIOCGNAME(sizeof(got)), got) > 0 &&
		    rt_streq(got, name))
			return fd;
		(void)S1(SYS_close, fd);
	}
	return -ENOENT;
}

static long uinput_make(const char *name)
{
	struct t_uinput_setup setup;
	long fd;
	int k;

	fd = S4(SYS_openat, AT_FDCWD, "/dev/uinput", O_RDWR, 0);
	if (fd < 0)
		return fd;
	(void)S3(SYS_ioctl, fd, T_UI_SET_EVBIT, T_EV_KEY);
	(void)S3(SYS_ioctl, fd, T_UI_SET_EVBIT, T_EV_REL);
	for (k = 0; k < 4; k++)
		(void)S3(SYS_ioctl, fd, T_UI_SET_KEYBIT, T_BTN_0 + k);
	(void)S3(SYS_ioctl, fd, T_UI_SET_RELBIT, T_REL_X);
	rt_fill(&setup, 0, sizeof(setup));
	setup.id.bustype = T_BUS_VIRTUAL;
	setup.id.vendor = 0x1234;
	setup.id.product = 0x5678;
	setup.id.version = 1;
	rt_copy(setup.name, name, rt_strlen(name) + 1);
	if (S3(SYS_ioctl, fd, T_UI_DEV_SETUP, &setup) != 0 ||
	    S2(SYS_ioctl, fd, T_UI_DEV_CREATE) != 0)
	{
		(void)S1(SYS_close, fd);
		return -EIO;
	}
	return fd;
}

/* A keyboard node (KEY_A), for a kernel without /dev/uinput. */
static long find_keyboard(int flags)
{
	int n;

	for (n = 0; n < 32; n++)
	{
		unsigned char ev[T_EV_LEN];
		unsigned char keys[T_KEY_LEN];
		struct t_input_id id;
		long fd = open_event(n, flags);

		if (fd < 0)
			continue;
		rt_fill(ev, 0, sizeof(ev));
		rt_fill(keys, 0, sizeof(keys));
		rt_fill(&id, 0, sizeof(id));
		(void)S3(SYS_ioctl, fd, T_EVIOCGBIT(0, sizeof(ev)), ev);
		(void)S3(SYS_ioctl, fd, T_EVIOCGBIT(T_EV_KEY, sizeof(keys)), keys);
		(void)S3(SYS_ioctl, fd, T_EVIOCGID, &id);
		if (bit_set(keys, T_KEY_A))
			return fd;
		(void)S1(SYS_close, fd);
	}
	return -ENOENT;
}

/* Open the device under test (non-blocking); < 0 without one. */
static long dut_open(void)
{
	dev_mount();
	if (g_ufd < 0)
		g_ufd = uinput_make(T_DEV_NAME);
	if (g_ufd >= 0)
	{
		g_key = T_BTN_0;
		return find_by_name(T_DEV_NAME, O_RDWR | O_NONBLOCK);
	}
	g_key = T_KEY_A;
	return find_keyboard(O_RDWR | O_NONBLOCK);
}

/* Inject one event: through uinput, or a write() to the event node. */
static long emit(long evfd, unsigned int type, unsigned int code, int value)
{
	struct t_input_event ev;

	rt_fill(&ev, 0, sizeof(ev));
	ev.type = (unsigned short)type;
	ev.code = (unsigned short)code;
	ev.value = value;
	return S3(SYS_write, g_ufd >= 0 ? g_ufd : evfd, &ev, sizeof(ev));
}

static long packet(long evfd, unsigned int code, int value)
{
	long r = emit(evfd, T_EV_KEY, code, value);

	if (r != (long)sizeof(struct t_input_event))
		return r;
	r = emit(evfd, T_EV_SYN, T_SYN_REPORT, 0);
	return r == (long)sizeof(struct t_input_event) ? 0 : r;
}

static long read_ev(long fd, struct t_input_event *ev)
{
	return S3(SYS_read, fd, ev, sizeof(*ev));
}

static long poll_in(long fd)
{
	struct t_pollfd p;
	struct t_ts zero = { 0, 0 };
	long r;

	p.fd = (int)fd;
	p.events = T_POLLIN;
	p.revents = 0;
	r = S5(SYS_ppoll, &p, 1, &zero, 0, 8);
	return r < 0 ? r : (p.revents & T_POLLIN) != 0;
}

static void drain(long fd)
{
	struct t_input_event ev;
	int i;

	for (i = 0; i < 256 && read_ev(fd, &ev) > 0; i++)
		;
}

/*
 * input.c input_register_device() for each serio port: atkbd and psmouse
 * make two devices, so no i8042 node mixes keyboard keys with relative
 * axes, and the keyboard has the AT name and phys of serio0.
 */
static int scan_i8042(int *kbd, int *mouse)
{
	int n;

	*kbd = 0;
	*mouse = 0;
	for (n = 0; n < 32; n++)
	{
		unsigned char ev[T_EV_LEN];
		unsigned char keys[T_KEY_LEN];
		unsigned char prop[8];
		struct t_input_id id;
		char phys[64];
		long fd = open_event(n, O_RDONLY | O_NONBLOCK);

		if (fd < 0)
			continue;
		rt_fill(ev, 0, sizeof(ev));
		rt_fill(keys, 0, sizeof(keys));
		rt_fill(prop, 0, sizeof(prop));
		rt_fill(&id, 0, sizeof(id));
		rt_fill(phys, 0, sizeof(phys));
		(void)S3(SYS_ioctl, fd, T_EVIOCGBIT(0, sizeof(ev)), ev);
		(void)S3(SYS_ioctl, fd, T_EVIOCGBIT(T_EV_KEY, sizeof(keys)), keys);
		(void)S3(SYS_ioctl, fd, T_EVIOCGID, &id);
		(void)S3(SYS_ioctl, fd, T_EVIOCGPROP(sizeof(prop)), prop);
		(void)S3(SYS_ioctl, fd, T_EVIOCGPHYS(sizeof(phys)), phys);
		(void)S1(SYS_close, fd);
		if (id.bustype != T_BUS_I8042)
			continue;
		CHECK(!(bit_set(keys, T_KEY_A) && bit_set(ev, T_EV_REL)));
		if (bit_set(keys, T_KEY_A))
		{
			(*kbd)++;
			CHECK(rt_streq(phys, "isa0060/serio0/input0"));
		}
		if (bit_set(ev, T_EV_REL))
		{
			(*mouse)++;
			CHECK(bit_set(keys, T_BTN_LEFT));
			CHECK(bit_set(prop, T_INPUT_PROP_POINTER));
			CHECK(rt_streq(phys, "isa0060/serio1/input0"));
		}
	}
	return CONF_PASS;
}

/*
 * input.c input_register_device() for each serio port: atkbd and psmouse
 * make two devices, so no i8042 node mixes keyboard keys with relative
 * axes; each has the phys of its serio port and the mouse is a pointer.
 * Linux probes the mouse port late, so wait for it up to five seconds.
 */
int input_i8042_keyboard_and_mouse_apart(void)
{
	int kbd;
	int mouse;
	int i;
	int r;

	dev_mount();
	for (i = 0; i < 50; i++)
	{
		r = scan_i8042(&kbd, &mouse);
		if (r != CONF_PASS)
			return r;
		if (kbd == 0 || mouse != 0)
			break;
		rt_sleep_ms(100);
	}
	/* A machine with an i8042 keyboard here also has the mouse. */
	EXPECT(kbd != 0, mouse != 0);
	return CONF_PASS;
}

/* evdev_read(): a buffer shorter than one event is EINVAL. */
int input_short_read_einval(void)
{
	char buf[8];
	long fd = dut_open();

	if (fd < 0)
		return CONF_TCONF;
	EXPECT_ERR(S3(SYS_read, fd, buf, sizeof(buf)), EINVAL);
	EXPECT_ERR(S3(SYS_read, fd, buf, sizeof(struct t_input_event)), EAGAIN);
	return CONF_PASS;
}

/* evdev_do_ioctl() ends in -EINVAL for any request it does not know. */
int input_unknown_ioctl_einval(void)
{
	unsigned int term[16];
	long fd = dut_open();

	if (fd < 0)
		return CONF_TCONF;
	EXPECT_ERR(S3(SYS_ioctl, fd, T_TCGETS, term), EINVAL);
	EXPECT_ERR(S3(SYS_ioctl, fd, T_IOR('E', 0x3f, 4), term), EINVAL);
	return CONF_PASS;
}

/*
 * bits_to_user(): EVIOCGBIT returns the bytes copied, the map length
 * capped by the caller's size; a type without a map is EINVAL.
 */
int input_gbit_returns_length(void)
{
	unsigned char bits[256];
	long fd = dut_open();

	if (fd < 0)
		return CONF_TCONF;
	EXPECT(S3(SYS_ioctl, fd, T_EVIOCGBIT(0, sizeof(bits)), bits), T_EV_LEN);
	EXPECT(S3(SYS_ioctl, fd, T_EVIOCGBIT(T_EV_KEY, sizeof(bits)), bits),
	       T_KEY_LEN);
	EXPECT(S3(SYS_ioctl, fd, T_EVIOCGBIT(T_EV_KEY, 4), bits), 4);
	EXPECT_ERR(S3(SYS_ioctl, fd, T_EVIOCGBIT(T_EV_REP, sizeof(bits)), bits),
		   EINVAL);
	EXPECT_ERR(S3(SYS_ioctl, fd, T_EVIOCGABS(T_ABS_X), bits), EINVAL);
	EXPECT(S3(SYS_ioctl, fd, T_EVIOCGPROP(sizeof(bits)), bits), 8);
	return CONF_PASS;
}

/* str_to_user(): the name with its NUL, capped; no phys is ENOENT. */
int input_name_and_phys(void)
{
	char name[64];
	long fd = dut_open();

	if (fd < 0)
		return CONF_TCONF;
	if (g_ufd < 0)
		return CONF_TCONF;
	rt_fill(name, 0, sizeof(name));
	EXPECT(S3(SYS_ioctl, fd, T_EVIOCGNAME(sizeof(name)), name),
	       (long)sizeof(T_DEV_NAME));
	CHECK(rt_streq(name, T_DEV_NAME));
	EXPECT(S3(SYS_ioctl, fd, T_EVIOCGNAME(3), name), 3);
	EXPECT_ERR(S3(SYS_ioctl, fd, T_EVIOCGPHYS(sizeof(name)), name), ENOENT);
	return CONF_PASS;
}

/* Every open has its own ring: two readers both get the packet. */
int input_each_open_has_a_ring(void)
{
	struct t_input_event ev;
	long a = dut_open();
	long b;

	if (a < 0)
		return CONF_TCONF;
	b = g_ufd >= 0 ? find_by_name(T_DEV_NAME, O_RDWR | O_NONBLOCK) :
			 find_keyboard(O_RDWR | O_NONBLOCK);
	CHECK(b >= 0);
	EXPECT(packet(a, g_key, 1), 0);
	EXPECT(read_ev(a, &ev), (long)sizeof(ev));
	EXPECT(ev.code, g_key);
	EXPECT(read_ev(b, &ev), (long)sizeof(ev));
	EXPECT(ev.type, T_EV_KEY);
	EXPECT(ev.code, g_key);
	EXPECT(ev.value, 1);
	EXPECT(read_ev(b, &ev), (long)sizeof(ev));
	EXPECT(ev.type, T_EV_SYN);
	EXPECT(packet(a, g_key, 0), 0);
	return CONF_PASS;
}

/*
 * evdev_fetch_next_event() stops at packet_head: an event without its
 * SYN_REPORT is not readable or pollable yet, and an empty report is not
 * passed on at all (input_handle_event() needs a value before the sync).
 */
int input_whole_packets_only(void)
{
	struct t_input_event ev;
	long fd = dut_open();

	if (fd < 0)
		return CONF_TCONF;
	drain(fd);
	EXPECT(emit(fd, T_EV_SYN, T_SYN_REPORT, 0), (long)sizeof(ev));
	EXPECT_ERR(read_ev(fd, &ev), EAGAIN);
	EXPECT(emit(fd, T_EV_KEY, g_key, 1), (long)sizeof(ev));
	EXPECT_ERR(read_ev(fd, &ev), EAGAIN);
	EXPECT(poll_in(fd), 0);
	EXPECT(emit(fd, T_EV_SYN, T_SYN_REPORT, 0), (long)sizeof(ev));
	EXPECT(poll_in(fd), 1);
	EXPECT(read_ev(fd, &ev), (long)sizeof(ev));
	EXPECT(ev.code, g_key);
	EXPECT(read_ev(fd, &ev), (long)sizeof(ev));
	EXPECT(ev.type, T_EV_SYN);
	EXPECT(packet(fd, g_key, 0), 0);
	return CONF_PASS;
}

/* __pass_event(): a full ring keeps SYN_DROPPED and the newest event. */
int input_overflow_syn_dropped(void)
{
	struct t_input_event ev;
	long fd = dut_open();
	int i;

	if (fd < 0)
		return CONF_TCONF;
	drain(fd);
	for (i = 0; i < 200; i++)
		EXPECT(packet(fd, g_key, (i & 1) ? 0 : 1), 0);
	EXPECT(read_ev(fd, &ev), (long)sizeof(ev));
	EXPECT(ev.type, T_EV_SYN);
	EXPECT(ev.code, T_SYN_DROPPED);
	drain(fd);
	return CONF_PASS;
}

/* evdev_grab()/evdev_ungrab(): one grabber; only it gets events. */
int input_grab_is_exclusive(void)
{
	struct t_input_event ev;
	long a = dut_open();
	long b;

	if (a < 0)
		return CONF_TCONF;
	b = g_ufd >= 0 ? find_by_name(T_DEV_NAME, O_RDWR | O_NONBLOCK) :
			 find_keyboard(O_RDWR | O_NONBLOCK);
	CHECK(b >= 0);
	drain(a);
	drain(b);
	EXPECT_ERR(S3(SYS_ioctl, a, T_EVIOCGRAB, 0), EINVAL);
	EXPECT(S3(SYS_ioctl, a, T_EVIOCGRAB, 1), 0);
	EXPECT_ERR(S3(SYS_ioctl, a, T_EVIOCGRAB, 1), EBUSY);
	EXPECT_ERR(S3(SYS_ioctl, b, T_EVIOCGRAB, 1), EBUSY);
	EXPECT_ERR(S3(SYS_ioctl, b, T_EVIOCGRAB, 0), EINVAL);
	if (g_ufd >= 0)
	{
		EXPECT(packet(a, g_key, 1), 0);
		EXPECT(read_ev(a, &ev), (long)sizeof(ev));
		EXPECT_ERR(read_ev(b, &ev), EAGAIN);
		EXPECT(packet(a, g_key, 0), 0);
	}
	EXPECT(S3(SYS_ioctl, a, T_EVIOCGRAB, 0), 0);
	EXPECT_ERR(S3(SYS_ioctl, a, T_EVIOCGRAB, 0), EINVAL);
	return CONF_PASS;
}

/* evdev_set_clk_type(): realtime, monotonic and boottime only. */
int input_clock_ids(void)
{
	int id;
	long fd = dut_open();

	if (fd < 0)
		return CONF_TCONF;
	id = 1;
	EXPECT(S3(SYS_ioctl, fd, T_EVIOCSCLOCKID, &id), 0);
	id = 7;
	EXPECT(S3(SYS_ioctl, fd, T_EVIOCSCLOCKID, &id), 0);
	id = 0;
	EXPECT(S3(SYS_ioctl, fd, T_EVIOCSCLOCKID, &id), 0);
	id = 2;
	EXPECT_ERR(S3(SYS_ioctl, fd, T_EVIOCSCLOCKID, &id), EINVAL);
	return CONF_PASS;
}

/* An event carries the time of the client's clock. */
int input_event_time_follows_clock(void)
{
	struct t_input_event ev;
	struct t_ts now;
	int id = 1;
	long fd = dut_open();

	if (fd < 0)
		return CONF_TCONF;
	drain(fd);
	EXPECT(S3(SYS_ioctl, fd, T_EVIOCSCLOCKID, &id), 0);
	EXPECT(packet(fd, g_key, 1), 0);
	EXPECT(S2(SYS_clock_gettime, 1, &now), 0);
	EXPECT(read_ev(fd, &ev), (long)sizeof(ev));
	CHECK(ev.sec >= now.sec - 2 && ev.sec <= now.sec);
	CHECK(ev.usec >= 0 && ev.usec < 1000000);
	EXPECT(packet(fd, g_key, 0), 0);
	return CONF_PASS;
}

/*
 * A clock change flushes the queue (its stamps are of the old clock) and
 * queues SYN_DROPPED, read once the next packet completes.
 */
int input_clock_change_flushes(void)
{
	struct t_input_event ev;
	int id = 1;
	long fd = dut_open();

	if (fd < 0)
		return CONF_TCONF;
	drain(fd);
	EXPECT(packet(fd, g_key, 1), 0);
	EXPECT(S3(SYS_ioctl, fd, T_EVIOCSCLOCKID, &id), 0);
	EXPECT_ERR(read_ev(fd, &ev), EAGAIN);
	EXPECT(packet(fd, g_key, 0), 0);
	EXPECT(read_ev(fd, &ev), (long)sizeof(ev));
	EXPECT(ev.type, T_EV_SYN);
	EXPECT(ev.code, T_SYN_DROPPED);
	EXPECT(read_ev(fd, &ev), (long)sizeof(ev));
	EXPECT(ev.code, g_key);
	EXPECT(ev.value, 0);
	return CONF_PASS;
}

/*
 * evdev_handle_get_val(): EVIOCGKEY returns the map length, has the held
 * key, and drops the queued key events (the report stays).
 */
int input_gkey_state_flushes_keys(void)
{
	unsigned char keys[T_KEY_LEN];
	struct t_input_event ev;
	long fd = dut_open();

	if (fd < 0)
		return CONF_TCONF;
	drain(fd);
	EXPECT(packet(fd, g_key, 1), 0);
	rt_fill(keys, 0, sizeof(keys));
	EXPECT(S3(SYS_ioctl, fd, T_EVIOCGKEY(sizeof(keys)), keys), T_KEY_LEN);
	CHECK(bit_set(keys, g_key));
	EXPECT(read_ev(fd, &ev), (long)sizeof(ev));
	EXPECT(ev.type, T_EV_SYN);
	EXPECT(ev.code, T_SYN_REPORT);
	EXPECT_ERR(read_ev(fd, &ev), EAGAIN);
	EXPECT(packet(fd, g_key, 0), 0);
	return CONF_PASS;
}

/* evdev_revoke(): the open stops working, others keep going. */
int input_revoke(void)
{
	struct t_input_event ev;
	long fd = dut_open();

	if (fd < 0)
		return CONF_TCONF;
	EXPECT_ERR(S3(SYS_ioctl, fd, T_EVIOCREVOKE, 1), EINVAL);
	EXPECT(S3(SYS_ioctl, fd, T_EVIOCREVOKE, 0), 0);
	EXPECT_ERR(read_ev(fd, &ev), ENODEV);
	EXPECT_ERR(S3(SYS_ioctl, fd, T_EVIOCGVERSION, &ev), ENODEV);
	return CONF_PASS;
}

/*
 * misc/uinput.c: UI_DEV_CREATE makes a device with the setup's identity;
 * UI_DEV_DESTROY removes its node and an open of it reads ENODEV.
 */
int input_uinput_device_lifecycle(void)
{
	struct t_input_event ev;
	struct t_input_id id;
	unsigned int version = 0;
	long u;
	long fd;

	dev_mount();
	u = uinput_make("ir0 input lifecycle");
	CHECK(u >= 0);
	EXPECT(S3(SYS_ioctl, u, T_UI_GET_VERSION, &version), 0);
	EXPECT(version, 5);
	fd = find_by_name("ir0 input lifecycle", O_RDONLY | O_NONBLOCK);
	CHECK(fd >= 0);
	rt_fill(&id, 0, sizeof(id));
	EXPECT(S3(SYS_ioctl, fd, T_EVIOCGID, &id), 0);
	EXPECT(id.bustype, T_BUS_VIRTUAL);
	EXPECT(id.vendor, 0x1234);
	EXPECT(id.product, 0x5678);
	EXPECT(S2(SYS_ioctl, u, T_UI_DEV_DESTROY), 0);
	EXPECT_ERR(read_ev(fd, &ev), ENODEV);
	EXPECT_ERR(find_by_name("ir0 input lifecycle", O_RDONLY), ENOENT);
	(void)S1(SYS_close, fd);
	(void)S1(SYS_close, u);
	return CONF_PASS;
}

/*
 * mousedev.c: every pointer feeds /dev/input/mice, read as three-byte
 * PS/2 packets (Y counts up); the node takes no ioctls.
 */
int input_mice_reads_ps2_packets(void)
{
	struct t_uinput_setup setup;
	unsigned char pkt[3];
	char name[8];
	long u;
	long m;

	dev_mount();
	m = S4(SYS_openat, AT_FDCWD, "/dev/input/mice", O_RDONLY | O_NONBLOCK, 0);
	CHECK(m >= 0);
	u = S4(SYS_openat, AT_FDCWD, "/dev/uinput", O_RDWR, 0);
	CHECK(u >= 0);
	EXPECT(S3(SYS_ioctl, u, T_UI_SET_EVBIT, T_EV_KEY), 0);
	EXPECT(S3(SYS_ioctl, u, T_UI_SET_EVBIT, T_EV_REL), 0);
	EXPECT(S3(SYS_ioctl, u, T_UI_SET_KEYBIT, T_BTN_LEFT), 0);
	EXPECT(S3(SYS_ioctl, u, T_UI_SET_RELBIT, T_REL_X), 0);
	EXPECT(S3(SYS_ioctl, u, T_UI_SET_RELBIT, T_REL_Y), 0);
	rt_fill(&setup, 0, sizeof(setup));
	setup.id.bustype = T_BUS_VIRTUAL;
	rt_copy(setup.name, "ir0 input mouse", sizeof("ir0 input mouse"));
	EXPECT(S3(SYS_ioctl, u, T_UI_DEV_SETUP, &setup), 0);
	EXPECT(S2(SYS_ioctl, u, T_UI_DEV_CREATE), 0);
	drain(m);
	g_ufd = u;
	EXPECT(emit(-1, T_EV_REL, T_REL_X, 5), (long)sizeof(struct t_input_event));
	EXPECT(emit(-1, T_EV_REL, T_REL_Y, -3), (long)sizeof(struct t_input_event));
	EXPECT(emit(-1, T_EV_KEY, T_BTN_LEFT, 1), (long)sizeof(struct t_input_event));
	EXPECT(emit(-1, T_EV_SYN, T_SYN_REPORT, 0), (long)sizeof(struct t_input_event));
	EXPECT(S3(SYS_read, m, pkt, sizeof(pkt)), 3);
	EXPECT(pkt[0], 0x09);
	EXPECT(pkt[1], 5);
	EXPECT(pkt[2], 3);
	EXPECT_ERR(S3(SYS_ioctl, m, T_EVIOCGNAME(sizeof(name)), name), ENOTTY);
	return CONF_PASS;
}
