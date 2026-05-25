/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * FASE58K — #UD exception diagnostics (serial tags on fault only).
 */

#include <ir0/fase58j_diag.h>
#include <ir0/copy_user.h>
#include <ir0/serial_io.h>
#include <kernel/process.h>
#include <mm/paging.h>
#include <string.h>

extern uint64_t iretq_checkpoint_buf[40];

static uint64_t f58j_last_syscall;
static uint64_t f58j_last_irq_vector;

void ir0_fase58j_note_syscall(uint64_t nr)
{
	f58j_last_syscall = nr;
}

void ir0_fase58j_note_irq(uint64_t vector)
{
	f58j_last_irq_vector = vector;
}

static void f58j_dump_rip_bytes(uint64_t rip, uint64_t cs, process_t *p)
{
	uint8_t bytes[8];
	size_t i;
	int ok = 0;

	memset(bytes, 0, sizeof(bytes));

	if ((cs & 3ULL) == 3ULL && p && p->page_directory)
	{
		if (copy_from_user_region_in_directory(p->page_directory,
						       (uintptr_t)rip, bytes,
						       sizeof(bytes)) == 0)
			ok = 1;
	}
	else if ((cs & 3ULL) == 0ULL && rip >= 0xFFFFFFFF80000000ULL)
	{
		memcpy(bytes, (const void *)(uintptr_t)rip, sizeof(bytes));
		ok = 1;
	}

	if (!ok)
		return;

	serial_print("UD_RIP_BYTES");
	for (i = 0; i < sizeof(bytes); i++)
	{
		serial_print(" b");
		serial_print_hex32((uint32_t)bytes[i]);
	}
	serial_print("\n");
}

void ir0_fase58j_ud_fault_report(uint64_t *stack)
{
	process_t *p = current_process;
	uint64_t rip;
	uint64_t cs;
	uint64_t rflags;
	uint64_t rsp;
	uint64_t ret_rip;
	const char *rip_class;

	if (!stack)
		return;

	rip = stack[2];
	cs = stack[3];
	rflags = stack[4];
	rsp = stack[5];
	ret_rip = iretq_checkpoint_buf[16];

	if ((cs & 3ULL) == 3ULL)
		rip_class = "USER";
	else
		rip_class = "KERNEL";

	serial_print("UD_FAULT_RIP=0x");
	serial_print_hex64(rip);
	serial_print("\n");

	serial_print("UD_FAULT_CS=0x");
	serial_print_hex64(cs);
	serial_print("\n");

	serial_print("UD_FAULT_RFLAGS=0x");
	serial_print_hex64(rflags);
	serial_print("\n");

	serial_print("UD_CURRENT_PID=");
	serial_print_hex32(p ? (uint32_t)p->task.pid : 0);
	serial_print("\n");

	serial_print("UD_CURRENT_NAME=");
	if (p && p->comm[0])
		serial_print(p->comm);
	else
		serial_print("(none)");
	serial_print("\n");

	serial_print("UD_LAST_SYSCALL=");
	serial_print_hex64(f58j_last_syscall);
	serial_print("\n");

	serial_print("UD_LAST_IRQ_VECTOR=");
	serial_print_hex64(f58j_last_irq_vector);
	serial_print("\n");

	serial_print("UD_RETURN_USER_RIP=0x");
	serial_print_hex64(ret_rip);
	serial_print("\n");

	serial_print("UD_FAULT_RSP=0x");
	serial_print_hex64(rsp);
	serial_print("\n");

	serial_print("UD_RIP_CLASS_");
	serial_print(rip_class);
	serial_print("\n");

	f58j_dump_rip_bytes(rip, cs, p);
}
