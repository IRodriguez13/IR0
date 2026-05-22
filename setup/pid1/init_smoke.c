/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 userspace init smoke — static ELF, no libc (ring-3 syscall path).
 *
 * Built by: make build-init-smoke
 * Loaded by: sudo make load-init
 * Boot with: CONFIG_KERNEL_DEBUG_SHELL=n
 */

#define SYS_write      1
#define SYS_exit_group 231

static long sys_write(int fd, const char *buf, unsigned long len)
{
	long ret;

	__asm__ volatile(
		"syscall"
		: "=a"(ret)
		: "a"(SYS_write), "D"(fd), "S"(buf), "d"(len)
		: "rcx", "r11", "memory");

	return ret;
}

static void sys_exit_group(int status)
{
	__asm__ volatile(
		"syscall"
		:
		: "a"(SYS_exit_group), "D"(status)
		: "rcx", "r11", "memory");

	for (;;)
		__asm__ volatile("hlt");
}

void _start(void)
{
	static const char msg[] = "IR0: init smoke (ring-3) ok\n";

	sys_write(1, msg, sizeof(msg) - 1);
	sys_exit_group(0);
}
