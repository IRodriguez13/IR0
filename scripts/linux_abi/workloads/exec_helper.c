/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: exec_helper.c
 * Description: Minimal static helper executed via execve for ABI audit
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <unistd.h>

int main(void)
{
	(void)write(1, "[LINUX_ABI_AUDIT][execve] step=1 op=helper_run ret=0 errno=0\n", 54);
	return 0;
}
