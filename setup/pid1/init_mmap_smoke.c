/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * FASE39 mmap smoke - validate mmap/munmap baseline behavior in ring 3.
 *
 * Sequence:
 * 1) mmap 16 KiB anonymous/private
 * 2) memset + verify
 * 3) munmap
 * 4) touch again (must trigger userspace PF -> SIGSEGV path)
 */

#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>

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
	size_t len = 16U * 1024U;
	unsigned char *m;
	int mapped = 0;
	int unmapped = 0;
	int ok = 1;

	m = (unsigned char *)mmap(NULL, len, PROT_READ | PROT_WRITE,
				    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (m == MAP_FAILED)
	{
		write_str("FASE39_MMAP FAIL mmap\n");
		return 2;
	}
	mapped = 1;

	for (size_t i = 0; i < len; i++)
		m[i] = (unsigned char)(i & 0xFFU);
	for (size_t i = 0; i < len; i++)
	{
		if (m[i] != (unsigned char)(i & 0xFFU))
		{
			ok = 0;
			break;
		}
	}

	if (munmap(m, len) == 0)
		unmapped = 1;
	else
		ok = 0;

	write_str("FASE39_MMAP mapped=");
	write_str(mapped ? "1" : "0");
	write_str(" unmapped=");
	write_str(unmapped ? "1" : "0");
	write_str(" verify=");
	write_str(ok ? "1" : "0");
	write_str(" addr=");
	write_hex_u64((uint64_t)(uintptr_t)m);
	write_str("\n");

	write_str("FASE39_MMAP touch_after_unmap=1 expected_segv=1\n");

	/*
	 * Must fault after munmap. If execution continues, munmap invalidation
	 * is broken and we return failure.
	 */
	((volatile unsigned char *)m)[0] = 0xA5;

	write_str("FASE39_MMAP FAIL no_segv_after_unmap\n");
	return 3;
}
