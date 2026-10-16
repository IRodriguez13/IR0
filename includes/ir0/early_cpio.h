/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: early_cpio.h
 * Description: Linux initramfs "newc"/"crc" CPIO header helpers (host-testable)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

/*
 * docs.kernel.org/driver-api/early-userspace/buffer-format.html
 * Header is 110 bytes of hex ASCII. Filename includes the trailing NUL.
 * Data begins at the next 4-byte boundary after the name.
 */
#define IR0_EARLY_CPIO_HEADER 110U
#define IR0_EARLY_CPIO_TRAILER "TRAILER!!!"

static inline int ir0_early_hex_nibble(uint8_t c)
{
	if (c >= '0' && c <= '9')
		return (int)(c - '0');
	if (c >= 'a' && c <= 'f')
		return (int)(c - 'a' + 10);
	if (c >= 'A' && c <= 'F')
		return (int)(c - 'A' + 10);
	return -1;
}

static inline int ir0_early_cpio_hex8(const uint8_t *src, uint32_t *out)
{
	uint32_t value = 0;
	unsigned i;
	int nibble;

	if (!src || !out)
		return 0;
	for (i = 0; i < 8; i++)
	{
		nibble = ir0_early_hex_nibble(src[i]);
		if (nibble < 0)
			return 0;
		value = (value << 4) | (uint32_t)nibble;
	}
	*out = value;
	return 1;
}

static inline int ir0_early_cpio_magic(const uint8_t *src)
{
	if (!src)
		return 0;
	if (src[0] != '0' || src[1] != '7' || src[2] != '0' || src[3] != '7' ||
	    src[4] != '0')
		return 0;
	return src[5] == '1' || src[5] == '2';
}

static inline uint32_t ir0_early_cpio_align4(uint32_t n)
{
	return (n + 3U) & ~3U;
}

static inline unsigned ir0_early_cpio_normalize(const char *in, char *out,
						unsigned cap)
{
	unsigned i = 0;

	if (!in || !out || cap < 2)
		return 0;
	if (in[0] == '.' && in[1] == '/')
		in += 2;
	while (in[0] == '/')
		in++;
	if (in[0] == '\0')
	{
		out[0] = '/';
		out[1] = '\0';
		return 1;
	}
	out[0] = '/';
	while (in[i] != '\0' && i + 2 < cap)
	{
		out[i + 1] = in[i];
		i++;
	}
	if (in[i] != '\0')
		return 0;
	out[i + 1] = '\0';
	return i + 1;
}

static inline int ir0_early_cpio_is_trailer(const char *name)
{
	const char *lit = IR0_EARLY_CPIO_TRAILER;
	unsigned i = 0;

	if (!name)
		return 0;
	while (lit[i] != '\0' && name[i] == lit[i])
		i++;
	return lit[i] == '\0' && name[i] == '\0';
}

struct ir0_early_cpio_member
{
	uint32_t mode;
	uint32_t filesize;
	uint32_t namesize;
	const char *name;
	const uint8_t *data;
	uint64_t next;
};

/*
 * Parse one newc member at @off. Returns 1 on a file, 0 on trailer/end,
 * -1 if the header or bounds are invalid.
 */
static inline int ir0_early_cpio_next(const uint8_t *buf, uint64_t cap,
				      uint64_t off,
				      struct ir0_early_cpio_member *out)
{
	uint32_t mode;
	uint32_t filesize;
	uint32_t namesize;
	uint64_t name_off;
	uint64_t data_off;
	uint64_t next;

	if (!buf || !out || off >= cap)
		return 0;
	while (off < cap && buf[off] == 0)
		off++;
	if (off + IR0_EARLY_CPIO_HEADER > cap)
		return 0;
	if (!ir0_early_cpio_magic(buf + off))
		return -1;
	if (!ir0_early_cpio_hex8(buf + off + 14, &mode) ||
	    !ir0_early_cpio_hex8(buf + off + 54, &filesize) ||
	    !ir0_early_cpio_hex8(buf + off + 94, &namesize))
		return -1;
	if (namesize == 0 || namesize > 4096)
		return -1;
	name_off = off + IR0_EARLY_CPIO_HEADER;
	if (name_off + namesize > cap)
		return -1;
	data_off = ir0_early_cpio_align4((uint32_t)(name_off + namesize));
	if (data_off + filesize > cap)
		return -1;
	next = ir0_early_cpio_align4((uint32_t)(data_off + filesize));
	out->mode = mode;
	out->filesize = filesize;
	out->namesize = namesize;
	out->name = (const char *)(buf + name_off);
	out->data = buf + data_off;
	out->next = next;
	if (ir0_early_cpio_is_trailer(out->name))
		return 0;
	return 1;
}
