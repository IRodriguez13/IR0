/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: oops_port.c
 * Description: x86_64 panic hooks: fault register names, user fault peeks
 *              and the iretq checkpoint buffer.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/oops_port.h>
#include <ir0/fault.h>
#include <ir0/copy_user.h>
#include <ir0/process.h>
#include <ir0/ktm/klog.h>

#include <stddef.h>
#include <stdint.h>

#define X86_VECTOR_GP  13U
#define X86_OPCODE_HLT 0xF4U

extern uint64_t iretq_checkpoint_buf[40];

void ir0_oops_show_fault_regs(const struct ir0_fault *fault)
{
	if (!fault)
		return;
	klog_print("vector=");
	klog_hex32(fault->hw_code);
	klog_print(" err=");
	klog_hex64(fault->syndrome);
	klog_print("\n");
	klog_print("fault_rip=");
	klog_hex64((uint64_t)fault->ip);
	klog_print(" fault_cs=");
	klog_hex64(fault->code_seg);
	klog_print("\n");
	klog_print("fault_rsp=");
	klog_hex64((uint64_t)fault->sp);
	klog_print(" fault_ss=");
	klog_hex64(fault->stack_seg);
	klog_print("\n");
	klog_print("fault_rflags=");
	klog_hex64(fault->status);
	klog_print("\n");
	klog_print("cr2=");
	if (fault->kind == IR0_FAULT_PAGE)
		klog_hex64((uint64_t)fault->addr);
	else
		klog_print("n/a (not a #PF; do not trust live CR2)");
	klog_print("\n");
}

/*
 * #GP and #PF in the current mm: peek the opcode at RIP (musl a_crash /
 * abort fallback is "endbr64; hlt") and the return address at [RSP]
 * (SysV call pushes it), which names the caller of the faulting code.
 */
const char *ir0_oops_user_fault_extra(const struct ir0_fault *fault,
				      struct process *proc,
				      const char *classify)
{
	unsigned char opc = 0;
	unsigned long long retaddr = 0;
	int have_opc = 0;
	int have_ret = 0;

	if (!fault || !proc || !process_pgd(proc))
		return classify;
	if (fault->kind != IR0_FAULT_PAGE && fault->hw_code != X86_VECTOR_GP)
		return classify;

	if (fault->hw_code == X86_VECTOR_GP &&
	    copy_from_user_mm(process_pgd(proc), fault->ip, &opc, 1) == 0)
	{
		have_opc = 1;
		if (opc == X86_OPCODE_HLT)
			classify = "USER_ABORT_HLT";
	}
	if (copy_from_user_mm(process_pgd(proc), fault->sp, &retaddr,
			      sizeof(retaddr)) == 0)
		have_ret = 1;
	if (have_opc || have_ret)
		klog_info_fmt("FAULT", "USER_FAULT_FRAME opc=%x ret=%llx",
			      (unsigned)(have_opc ? opc : 0),
			      (unsigned long long)(have_ret ? retaddr : 0));
	return classify;
}

void ir0_oops_dump_arch_extra(void)
{
	size_t k;

	klog_print("\n--- IRETQ CHECKPOINT BUFFER ---\n");
	for (k = 0; k < 40; k++)
	{
		klog_print("ckpt[");
		klog_hex32((uint32_t)k);
		klog_print("] = ");
		klog_hex64(iretq_checkpoint_buf[k]);
		klog_print("\n");
	}
}
