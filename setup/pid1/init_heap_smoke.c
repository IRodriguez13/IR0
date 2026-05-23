/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * FASE39 heap smoke - validate brk/sbrk minimal behavior in ring 3.
 *
 * Report fields:
 * - heap_start
 * - heap_end
 * - brk_before
 * - brk_after
 * - page_present (write/read roundtrip on sbrk memory)
 */

#include <stdint.h>
#include <unistd.h>
#include <sys/syscall.h>

#define IR0_USER_HEAP_BASE 0x02000000UL

static void write_str(const char *s)
{
	const char *p = s;

	while (*p)
		p++;
	(void)write(1, s, (size_t)(p - s));
}

static void write_hex_u64(uint64_t v)
{
	static const char hex[] = "0123456789ABCDEF";
	char out[18];

	out[0] = '0';
	out[1] = 'x';
	for (int i = 0; i < 16; i++)
	{
		unsigned shift = (unsigned)(60 - (i * 4));

		out[2 + i] = hex[(v >> shift) & 0xFU];
	}
	(void)write(1, out, sizeof(out));
}

int main(void)
{
	uintptr_t brk_before;
	uintptr_t brk_after;
	uintptr_t brk_target;
	uintptr_t heap_start;
	unsigned char *p;
	unsigned char expected = 0x5A;
	int page_present = 0;
	int sbrk_ok = 1;

	brk_before = (uintptr_t)syscall(SYS_brk, 0);
	p = (unsigned char *)sbrk(4096);
	if (p == (void *)-1)
	{
		uintptr_t base = brk_before;

		sbrk_ok = 0;
		if (base < IR0_USER_HEAP_BASE)
			base = IR0_USER_HEAP_BASE;
		p = (unsigned char *)base;
		brk_target = base + 4096U;
		if ((uintptr_t)syscall(SYS_brk, (void *)brk_target) != brk_target)
		{
			write_str("FASE39_HEAP FAIL sbrk_and_brk\n");
			return 2;
		}
	}

	*p = expected;
	if (*p == expected)
		page_present = 1;

	brk_after = (uintptr_t)syscall(SYS_brk, 0);
	heap_start = (uintptr_t)p;

	write_str("FASE39_HEAP heap_start=");
	write_hex_u64((uint64_t)heap_start);
	write_str(" heap_end=");
	write_hex_u64((uint64_t)brk_after);
	write_str(" brk_before=");
	write_hex_u64((uint64_t)brk_before);
	write_str(" brk_after=");
	write_hex_u64((uint64_t)brk_after);
	write_str(" page_present=");
	write_str(page_present ? "1" : "0");
	write_str(" sbrk_ok=");
	write_str(sbrk_ok ? "1" : "0");
	write_str("\n");

	return page_present ? 0 : 3;
}
