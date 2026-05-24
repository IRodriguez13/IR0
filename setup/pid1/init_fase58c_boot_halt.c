/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * FASE58C — Hold after kernel boot direct draw (no userspace FB writes).
 */

#include <unistd.h>

static void write_str(const char *s)
{
	const char *p = s;

	while (*p)
		p++;
	(void)write(1, s, (size_t)(p - s));
}

int main(void)
{
	write_str("FASE58C_BOOT_HALT\n");
	write_str("FASE58C_BOOT_GUI_HOLD\n");
	write_str("FASE58C_OK\n");

	for (;;)
		(void)pause();

	return 0;
}
