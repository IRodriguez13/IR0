/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _LINUX_APM_H
#define _LINUX_APM_H

typedef unsigned short apm_event_t;

#define APM_SYS_STANDBY       0x0001
#define APM_SYS_SUSPEND       0x0002
#define APM_NORMAL_RESUME     0x0003
#define APM_CRITICAL_RESUME   0x0004
#define APM_CRITICAL_SUSPEND  0x0008
#define APM_USER_STANDBY      0x0009
#define APM_USER_SUSPEND      0x000a
#define APM_STANDBY_RESUME    0x000b

#define APM_IOC_STANDBY 0x4101
#define APM_IOC_SUSPEND 0x4102

#endif
