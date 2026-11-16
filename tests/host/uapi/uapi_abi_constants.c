/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: uapi_abi_constants.c
 * Description: Compile-time Linux ABI values of the exported UAPI headers.
 *
 * Built by scripts/uapi_check.py against the `make headers_install` tree of
 * every ISA.  Each value is the Linux UAPI value an unmodified program (here
 * BusyBox 1.36.1 console-tools and miscutils) compiles into its binary.
 */

#include <stddef.h>
#include <linux/kd.h>

#define UAPI_EQ(expr, value) _Static_assert((expr) == (value), #expr " != " #value)

/* Keyboard modes (KDGKBMODE / KDSKBMODE). */
UAPI_EQ(K_RAW, 0x00);
UAPI_EQ(K_XLATE, 0x01);
UAPI_EQ(K_MEDIUMRAW, 0x02);
UAPI_EQ(K_UNICODE, 0x03);
UAPI_EQ(K_OFF, 0x04);

/* Console ioctls. */
UAPI_EQ(KIOCSOUND, 0x4B2F);
UAPI_EQ(KDMKTONE, 0x4B30);
UAPI_EQ(KDSETLED, 0x4B32);
UAPI_EQ(KDSETMODE, 0x4B3A);
UAPI_EQ(KDGETMODE, 0x4B3B);
UAPI_EQ(KDGKBMODE, 0x4B44);
UAPI_EQ(KDSKBMODE, 0x4B45);
UAPI_EQ(KDGKBENT, 0x4B46);
UAPI_EQ(PIO_SCRNMAP, 0x4B41);
UAPI_EQ(PIO_UNIMAP, 0x4B67);
UAPI_EQ(PIO_UNIMAPCLR, 0x4B68);
UAPI_EQ(PIO_UNISCRNMAP, 0x4B6A);
UAPI_EQ(E_TABSZ, 256);

/* Unicode font-map structures passed through PIO_UNIMAP / PIO_UNIMAPCLR. */
UAPI_EQ(sizeof(struct unipair), 4);
UAPI_EQ(offsetof(struct unipair, unicode), 0);
UAPI_EQ(offsetof(struct unipair, fontpos), 2);
UAPI_EQ(offsetof(struct unimapdesc, entry_ct), 0);
UAPI_EQ(offsetof(struct unimapdesc, entries), sizeof(void *));
UAPI_EQ(sizeof(struct unimapdesc), 2 * sizeof(void *));
UAPI_EQ(sizeof(struct unimapinit), 6);
UAPI_EQ(offsetof(struct unimapinit, advised_hashsize), 0);
UAPI_EQ(offsetof(struct unimapinit, advised_hashstep), 2);
UAPI_EQ(offsetof(struct unimapinit, advised_hashlevel), 4);
