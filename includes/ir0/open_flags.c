// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: open_flags.c
 * Description: Linux open(2) ABI → IR0 internal open flags translation.
 *
 * Source: Linux x86-64 open flag layout (musl arch/x86_64/syscall_arch.h,
 *         Linux uapi asm-generic/fcntl.h).
 */

#include "open_flags.h"
#include <ir0/serial_io.h>

int linux_open_flags_to_ir0(int linux_flags)
{
    int ir0 = linux_flags & IR0_O_ACCMODE;

    if (linux_flags & (int)LINUX_O_CREAT)
        ir0 |= IR0_O_CREAT;
    if (linux_flags & (int)LINUX_O_EXCL)
        ir0 |= IR0_O_EXCL;
    if (linux_flags & (int)LINUX_O_TRUNC)
        ir0 |= IR0_O_TRUNC;
    if (linux_flags & (int)LINUX_O_APPEND)
        ir0 |= IR0_O_APPEND;
    if (linux_flags & (int)LINUX_O_NONBLOCK)
        ir0 |= IR0_O_NONBLOCK;
    if (linux_flags & (int)LINUX_O_DIRECTORY)
        ir0 |= IR0_O_DIRECTORY;
    if (linux_flags & (int)LINUX_O_CLOEXEC)
        ir0 |= IR0_O_CLOEXEC;

    return ir0;
}

int ir0_open_flags_ok_for_vfs(int flags)
{
    uint32_t f = (uint32_t)flags;

    /*
     * Unambiguous Linux-only bits (no IR0 equivalent at these positions).
     */
    if (f & LINUX_O_CREAT)
        return 0;
    if (f & LINUX_O_EXCL)
        return 0;

    /*
     * Linux O_DIRECTORY (0x10000) vs IR0_O_DIRECTORY (0x200000).
     * O_LARGEFILE (0x8000) is musl noise on x86-64 — ignore.
     */
    if ((f & LINUX_O_DIRECTORY) && !(f & IR0_O_DIRECTORY))
        return 0;
    if (f & LINUX_O_NOFOLLOW)
        return 0;

    /*
     * Linux O_TRUNC (0x200) overlaps IR0_O_EXCL (0x200).  After translation,
     * O_TRUNC becomes IR0_O_TRUNC (0x400); raw Linux O_TRUNC leaks as 0x200
     * without either IR0 status bit.
     */
    if ((f & LINUX_O_TRUNC) && !(f & IR0_O_EXCL) && !(f & IR0_O_TRUNC))
        return 0;

    /*
     * Do not compare LINUX_O_APPEND (0x400) with IR0_O_APPEND: that bit
     * position is IR0_O_TRUNC after translation.
     */
    return 1;
}

void ir0_open_flags_log_translation(int linux_raw, int ir0_flags)
{
    serial_print("[IR0_OPEN_ABI] raw=");
    serial_print_hex64((uint64_t)(uint32_t)linux_raw);
    serial_print(" ir0=");
    serial_print_hex64((uint64_t)(uint32_t)ir0_flags);
    if (ir0_flags & IR0_O_CREAT)
        serial_print(" IR0_O_CREAT");
    if (ir0_flags & IR0_O_TRUNC)
        serial_print(" IR0_O_TRUNC");
    if (ir0_flags & IR0_O_EXCL)
        serial_print(" IR0_O_EXCL");
    if (ir0_flags & IR0_O_APPEND)
        serial_print(" IR0_O_APPEND");
    if (ir0_flags & IR0_O_NONBLOCK)
        serial_print(" IR0_O_NONBLOCK");
    if (ir0_flags & IR0_O_DIRECTORY)
        serial_print(" IR0_O_DIRECTORY");
    if (ir0_flags & IR0_O_CLOEXEC)
        serial_print(" IR0_O_CLOEXEC");
    if ((ir0_flags & IR0_O_ACCMODE) == IR0_O_WRONLY)
        serial_print(" IR0_O_WRONLY");
    else if ((ir0_flags & IR0_O_ACCMODE) == IR0_O_RDWR)
        serial_print(" IR0_O_RDWR");
    else if ((ir0_flags & IR0_O_ACCMODE) == IR0_O_RDONLY)
        serial_print(" IR0_O_RDONLY");
    serial_print("\n");

    if (ir0_open_flags_ok_for_vfs(ir0_flags))
        serial_print("[IR0_OPEN_ABI][CLASSIFY] OPEN_ABI_TRANSLATION_LAYER_OK\n");
}
