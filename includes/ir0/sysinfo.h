/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 - Linux struct sysinfo (musl compatibility)
 */
#ifndef _IR0_SYSINFO_H
#define _IR0_SYSINFO_H

struct sysinfo {
    unsigned long uptime;
    unsigned long loads[3];
    unsigned long totalram;
    unsigned long freeram;
    unsigned long sharedram;
    unsigned long bufferram;
    unsigned long totalswap;
    unsigned long freeswap;
    unsigned short procs;
    unsigned short pad;
    unsigned long totalhigh;
    unsigned long freehigh;
    unsigned int mem_unit;
    char __reserved[256];
};

#endif /* _IR0_SYSINFO_H */
