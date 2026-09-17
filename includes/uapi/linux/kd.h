/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _LINUX_KD_H
#define _LINUX_KD_H

#define KDMKTONE    0x4B30
#define KDSETLED    0x4B32
#define KDSETMODE   0x4B3A
#define KDGETMODE   0x4B3B
#define KD_TEXT     0x00
#define KD_GRAPHICS 0x01
#define KDGKBMODE   0x4B44
#define KDSKBMODE   0x4B45
#define KDGKBENT    0x4B46
#define K_MEDIUMRAW 0x02

struct kbentry {
	unsigned char kb_table;
	unsigned char kb_index;
	unsigned short kb_value;
};

#endif
