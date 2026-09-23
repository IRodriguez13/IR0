/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: pt_interp_freestanding.c
 * Description: Tiny ET_EXEC / ET_DYN bodies for PT_INTERP QEMU (no libc)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#define SYS_write 1
#define SYS_exit 60

static long sys3(long n, long a, long b, long c)
{
	long r;

	__asm__ volatile("syscall"
			 : "=a"(r)
			 : "a"(n), "D"(a), "S"(b), "d"(c)
			 : "rcx", "r11", "memory");
	return r;
}

#ifdef PT_INTERP_LDSO
static const char msg[] = "PT_INTERP_OK\n";
static const long msg_len = 13;
static const long exit_code = 0;
#else
static const char msg[] = "MAIN_NO_INTERP\n";
static const long msg_len = 15;
static const long exit_code = 1;
#endif

void _start(void)
{
	sys3(SYS_write, 1, (long)msg, msg_len);
	sys3(SYS_exit, exit_code, 0, 0);
	for (;;)
		;
}
