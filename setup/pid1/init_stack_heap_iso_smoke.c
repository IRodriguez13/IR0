/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * FASE39 stack/heap isolation smoke.
 *
 * Performs deep recursion + heap growth and reports whether virtual ranges
 * overlap for this process.
 */

#include <stdint.h>
#include <unistd.h>
#include <sys/syscall.h>

#define IR0_USER_HEAP_BASE 0x02000000UL

static uintptr_t stack_low = ~(uintptr_t)0;
static uintptr_t stack_high = 0;
static volatile uint64_t stack_sink = 0;

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

static void write_dec_u64(uint64_t v)
{
	char buf[32];
	int n = 0;

	if (v == 0)
	{
		(void)write(1, "0", 1);
		return;
	}

	while (v > 0 && n < (int)sizeof(buf))
	{
		buf[n++] = (char)('0' + (v % 10U));
		v /= 10U;
	}
	while (n-- > 0)
		(void)write(1, &buf[n], 1);
}

static void recurse_depth(int depth)
{
	volatile unsigned char marker[256];
	uintptr_t lo = (uintptr_t)&marker[0];
	uintptr_t hi = lo + sizeof(marker);

	marker[0] = (unsigned char)depth;
	marker[sizeof(marker) - 1] = (unsigned char)(depth ^ 0x5A);

	if (lo < stack_low)
		stack_low = lo;
	if (hi > stack_high)
		stack_high = hi;

	if (depth > 0)
		recurse_depth(depth - 1);

	stack_sink += marker[0];
}

int main(void)
{
	uintptr_t brk_before;
	uintptr_t brk_after;
	uintptr_t brk_target;
	uintptr_t heap_start;
	uint64_t stack_pages;
	uint64_t heap_pages;
	int overlap;
	int sbrk_ok = 1;
	volatile unsigned char *heap_ptr;

	brk_before = (uintptr_t)syscall(SYS_brk, 0);
	heap_ptr = (volatile unsigned char *)sbrk(8192);
	if (heap_ptr == (void *)-1)
	{
		uintptr_t base = brk_before;

		sbrk_ok = 0;
		if (base < IR0_USER_HEAP_BASE)
			base = IR0_USER_HEAP_BASE;
		heap_ptr = (volatile unsigned char *)base;
		brk_target = base + 8192U;
		if ((uintptr_t)syscall(SYS_brk, (void *)brk_target) != brk_target)
		{
			write_str("FASE39_ISO FAIL sbrk_and_brk\n");
			return 2;
		}
	}
	heap_ptr[0] = 0x11;
	heap_ptr[4096] = 0x22;
	brk_after = (uintptr_t)syscall(SYS_brk, 0);
	heap_start = (uintptr_t)heap_ptr;

	recurse_depth(20);

	stack_pages = (stack_high > stack_low)
			  ? ((uint64_t)(stack_high - stack_low) + 4095U) / 4096U
			  : 0;
	heap_pages = (brk_after > heap_start)
			 ? ((uint64_t)(brk_after - heap_start) + 4095U) / 4096U
			 : 0;
	overlap = !((brk_after <= stack_low) || (stack_high <= heap_start));

	write_str("FASE39_ISO stack_pages=");
	write_dec_u64(stack_pages);
	write_str(" heap_pages=");
	write_dec_u64(heap_pages);
	write_str(" overlap=");
	write_str(overlap ? "1" : "0");
	write_str(" sbrk_ok=");
	write_str(sbrk_ok ? "1" : "0");
	write_str(" stack_range=[");
	write_hex_u64((uint64_t)stack_low);
	write_str(",");
	write_hex_u64((uint64_t)stack_high);
	write_str(") heap_range=[");
	write_hex_u64((uint64_t)heap_start);
	write_str(",");
	write_hex_u64((uint64_t)brk_after);
	write_str(")\n");

	return overlap ? 3 : 0;
}
