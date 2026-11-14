/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: test_kernel_string.c
 * Description: Host unit tests for the freestanding kernel string library.
 */

#include "test_harness.h"
#include "kernel_string.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void check_region(const char *what, const unsigned char *p,
			 size_t n, unsigned char value)
{
	size_t i;

	for (i = 0; i < n; i++)
	{
		if (p[i] != value)
		{
			fprintf(stderr, "\n[TEST] %s: byte %zu is 0x%02x, "
					"expected 0x%02x\n",
				what, i, p[i], value);
			_ir0_test_failed = 1;
			return;
		}
	}
}

void test_kernel_string_printf_width(void)
{
	char buf[128];
	size_t i;

	TEST_BEGIN("kernel_vsnprintf width and precision");

	ir0_snprintf(buf, sizeof(buf), "[%-20s]", "hda");
	ASSERT_EQ(strlen(buf), (size_t)22);
	ASSERT_EQ(buf[0], '[');
	ASSERT_EQ(buf[1], 'h');
	ASSERT_EQ(buf[2], 'd');
	ASSERT_EQ(buf[3], 'a');
	for (i = 4; i < 21; i++)
		ASSERT_EQ(buf[i], ' ');
	ASSERT_EQ(buf[21], ']');

	ir0_snprintf(buf, sizeof(buf), "[%20s]", "hda");
	ASSERT_EQ(strlen(buf), (size_t)22);
	ASSERT_EQ(buf[0], '[');
	for (i = 1; i < 18; i++)
		ASSERT_EQ(buf[i], ' ');
	ASSERT_EQ(buf[18], 'h');
	ASSERT_EQ(buf[20], 'a');
	ASSERT_EQ(buf[21], ']');

	ir0_snprintf(buf, sizeof(buf), "%.3s", "abcdef");
	ASSERT_STR_EQ(buf, "abc");
	ir0_snprintf(buf, sizeof(buf), "%8.3s", "abcdef");
	ASSERT_STR_EQ(buf, "     abc");
	ir0_snprintf(buf, sizeof(buf), "%-8.3s", "abcdef");
	ASSERT_STR_EQ(buf, "abc     ");

	ir0_snprintf(buf, sizeof(buf), "%5c", 'Z');
	ASSERT_STR_EQ(buf, "    Z");
	ir0_snprintf(buf, sizeof(buf), "%-5c", 'Z');
	ASSERT_STR_EQ(buf, "Z    ");

	ir0_snprintf(buf, sizeof(buf), "%08x", 0x2aU);
	ASSERT_STR_EQ(buf, "0000002a");
	ir0_snprintf(buf, sizeof(buf), "%8d", -42);
	ASSERT_STR_EQ(buf, "     -42");
	ir0_snprintf(buf, sizeof(buf), "%zu", (size_t)1234);
	ASSERT_STR_EQ(buf, "1234");
	ir0_snprintf(buf, sizeof(buf), "%016llx", 0x123456789abcULL);
	ASSERT_STR_EQ(buf, "0000123456789abc");

	ir0_snprintf(buf, sizeof(buf), "%s", (char *)0);
	ASSERT_STR_EQ(buf, "(null)");

	ir0_snprintf(buf, 8, "0123456789");
	ASSERT_STR_EQ(buf, "0123456");

	TEST_END();
}

void test_kernel_string_length(void)
{
	char buf[256];
	size_t offset;
	size_t len;

	TEST_BEGIN("kernel strlen over every alignment");

	for (offset = 0; offset < 16; offset++)
	{
		for (len = 0; len < 200; len++)
		{
			memset(buf, 'a', sizeof(buf));
			buf[offset + len] = '\0';
			ASSERT_EQ(ir0_strlen(buf + offset), len);
		}
	}

	TEST_END();
}

void test_kernel_string_memset(void)
{
	unsigned char area[160];
	size_t offset;
	size_t len;
	unsigned char values[] = { 0x00, 0x5a, 0xff };

	TEST_BEGIN("kernel memset preserves guard bytes");

	for (size_t v = 0; v < sizeof(values); v++)
	{
		for (offset = 0; offset < 16; offset++)
		{
			for (len = 0; len <= 64; len++)
			{
				memset(area, 0xa5, sizeof(area));
				ASSERT(ir0_memset(area + offset, values[v], len) ==
				       area + offset);
				check_region("memset body", area + offset, len,
					     values[v]);
				check_region("memset before", area, offset, 0xa5);
				check_region("memset after",
					     area + offset + len,
					     sizeof(area) - offset - len, 0xa5);
			}
		}
	}

	TEST_END();
}

void test_kernel_string_memcpy(void)
{
	unsigned char src[192];
	unsigned char dst[192];
	size_t src_off;
	size_t dst_off;
	size_t len;

	TEST_BEGIN("kernel memcpy over every source/dest alignment");

	for (size_t i = 0; i < sizeof(src); i++)
		src[i] = (unsigned char)(i * 7 + 3);

	for (src_off = 0; src_off < 16; src_off++)
	{
		for (dst_off = 0; dst_off < 16; dst_off++)
		{
			for (len = 0; len <= 64; len++)
			{
				memset(dst, 0xa5, sizeof(dst));
				ASSERT(ir0_memcpy(dst + dst_off, src + src_off,
						  len) == dst + dst_off);
				ASSERT(memcmp(dst + dst_off, src + src_off,
					      len) == 0);
				if (dst_off)
					check_region("memcpy before", dst,
						     dst_off, 0xa5);
				check_region("memcpy after", dst + dst_off + len,
					     sizeof(dst) - dst_off - len, 0xa5);
			}
		}
	}

	TEST_END();
}

static void reference_move(unsigned char *dst, const unsigned char *src,
			   size_t n)
{
	if (dst < src)
	{
		for (size_t i = 0; i < n; i++)
			dst[i] = src[i];
	}
	else if (dst > src)
	{
		for (size_t i = n; i > 0; i--)
			dst[i - 1] = src[i - 1];
	}
}

void test_kernel_string_memmove(void)
{
	unsigned char got[128];
	unsigned char want[128];
	size_t dst_off;
	size_t src_off;
	size_t len;

	TEST_BEGIN("kernel memmove matches a byte reference");

	for (dst_off = 0; dst_off < 16; dst_off++)
	{
		for (src_off = 0; src_off < 16; src_off++)
		{
			for (len = 1; len <= 48; len++)
			{
				for (size_t i = 0; i < sizeof(got); i++)
				{
					got[i] = (unsigned char)(i * 11 + 1);
					want[i] = got[i];
				}
				ASSERT(ir0_memmove(got + dst_off, got + src_off,
						   len) == got + dst_off);
				reference_move(want + dst_off, want + src_off, len);
				ASSERT(memcmp(got, want, sizeof(got)) == 0);
			}
		}
	}

	/* No-op moves must not even touch the buffer. */
	memset(got, 0x5a, sizeof(got));
	ASSERT(ir0_memmove(got, got, sizeof(got)) == got);
	check_region("memmove same pointer", got, sizeof(got), 0x5a);

	TEST_END();
}

void test_kernel_string_memcmp(void)
{
	unsigned char a[96];
	unsigned char b[96];
	size_t offset;
	size_t len;
	size_t diff;

	TEST_BEGIN("kernel memcmp over every alignment and diff position");

	for (offset = 0; offset < 16; offset++)
	{
		for (len = 1; len <= 48; len++)
		{
			for (size_t i = 0; i < sizeof(a); i++)
				a[i] = b[i] = (unsigned char)(i * 5 + 9);
			ASSERT_EQ(ir0_memcmp(a + offset, b + offset, len), 0);

			for (diff = 0; diff < len; diff++)
			{
				unsigned char saved = b[offset + diff];

				a[offset + diff] = 0x80;
				b[offset + diff] = 0x7f;
				ASSERT(ir0_memcmp(a + offset, b + offset, len) > 0);
				a[offset + diff] = 0x7f;
				b[offset + diff] = 0x80;
				ASSERT(ir0_memcmp(a + offset, b + offset, len) < 0);
				a[offset + diff] = saved;
				b[offset + diff] = saved;
			}
		}
	}

	TEST_END();
}

void test_kernel_string_copy_pad(void)
{
	char buf[16];

	TEST_BEGIN("kernel strncpy pads and strncat bounds");

	memset(buf, 0x5a, sizeof(buf));
	ASSERT(ir0_strncpy(buf, "abc", 6) == buf);
	ASSERT_STR_EQ(buf, "abc");
	check_region("strncpy pad", (unsigned char *)buf + 4, 2, 0x00);
	ASSERT_EQ(buf[6], 0x5a);

	memset(buf, 0x5a, sizeof(buf));
	ir0_strcpy(buf, "ab");
	ASSERT(ir0_strncat(buf, "cdef", 3) == buf);
	ASSERT_STR_EQ(buf, "abcde");

	TEST_END();
}
