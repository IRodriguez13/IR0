/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 - utsname structure (Linux/musl uname compatibility)
 *
 * Matches Linux struct utsname for uname(2) syscall.
 */
#ifndef _IR0_UTSNAME_H
#define _IR0_UTSNAME_H

#define _UTSNAME_LENGTH 65

struct utsname {
    char sysname[_UTSNAME_LENGTH];
    char nodename[_UTSNAME_LENGTH];
    char release[_UTSNAME_LENGTH];
    char version[_UTSNAME_LENGTH];
    char machine[_UTSNAME_LENGTH];
};

#endif /* _IR0_UTSNAME_H */
