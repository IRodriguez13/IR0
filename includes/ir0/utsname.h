/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: utsname.h
 * Description: IR0 kernel source/header file
 */

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
    char domainname[_UTSNAME_LENGTH];
};

static inline void ir0_utsname_copy_field(char *destination,
					  const char *source)
{
	unsigned int index = 0;

	if (source)
		while (index + 1 < _UTSNAME_LENGTH && source[index]) {
			destination[index] = source[index];
			index++;
		}
	while (index < _UTSNAME_LENGTH)
		destination[index++] = '\0';
}

static inline void ir0_utsname_init(struct utsname *value,
				    const char *sysname,
				    const char *nodename,
				    const char *release,
				    const char *version,
				    const char *machine)
{
	if (!value)
		return;
	ir0_utsname_copy_field(value->sysname, sysname);
	ir0_utsname_copy_field(value->nodename, nodename);
	ir0_utsname_copy_field(value->release, release);
	ir0_utsname_copy_field(value->version, version);
	ir0_utsname_copy_field(value->machine, machine);
	ir0_utsname_copy_field(value->domainname, "");
}

_Static_assert(sizeof(struct utsname) == 390,
	       "Linux new_utsname ABI must contain six 65-byte fields");

#endif /* _IR0_UTSNAME_H */
