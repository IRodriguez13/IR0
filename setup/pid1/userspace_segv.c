/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Minimal userspace SIGSEGV trigger for PF handling smoke.
 */

int main(void)
{
	volatile int *p = (int *)0xDEADBEEF;

	*p = 1;
	return 0;
}
