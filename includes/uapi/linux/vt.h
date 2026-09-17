/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _LINUX_VT_H
#define _LINUX_VT_H

#define VT_OPENQRY      0x5600
#define VT_GETMODE      0x5601
#define VT_SETMODE      0x5602
#define VT_GETSTATE     0x5603
#define VT_RELDISP      0x5605
#define VT_ACTIVATE     0x5606
#define VT_WAITACTIVE   0x5607
#define VT_DISALLOCATE  0x5608

#define VT_AUTO         0x00
#define VT_PROCESS      0x01
#define VT_ACKACQ       0x02

struct vt_mode {
	char mode;
	char waitv;
	short relsig;
	short acqsig;
	short frsig;
};

struct vt_stat {
	unsigned short v_active;
	unsigned short v_signal;
	unsigned short v_state;
};

#endif
