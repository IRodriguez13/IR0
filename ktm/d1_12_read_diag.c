/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: d1_12_read_diag.c
 * Description: D1.12 read/syscall-return integrity diagnostics
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <d1_12_read_diag.h>
#include <ir0/tty_smoke.h>
#include <ir0/copy_user.h>
#include <ir0/process.h>
#include <ir0/ktm/klog.h>
#include <string.h>

#define D1_12_MAGIC_LEN  0x1AF43FUL
#define D1_12_PREVIEW    32

struct d1_12_last_read
{
	uint32_t seq;
	int      valid;
	int      fd;
	uintptr_t user_buf;
	size_t   req_count;
	int64_t  ret;
	size_t   kcopy_len;
	uint8_t  kpreview[D1_12_PREVIEW];
	uint8_t  upreview[D1_12_PREVIEW];
	uint64_t pre_ret;
	uint64_t pre_arg0;
	uint64_t pre_arg1;
	uint64_t pre_arg2;
	uint64_t pre_pad;
	uint64_t pre_arg4;
	uint64_t pre_arg5;
	uint64_t pre_arg3;
	uint64_t pre_pc;
	uint64_t pre_sp;
	uint64_t post_ret;
	uint64_t restore_arg2;
	uint64_t restore_arg1;
	uint64_t restore_arg0;
	size_t   tty_line_len;
};

static struct d1_12_last_read d1_12_last;
static struct d1_12_last_read d1_12_last_sh;
static uint32_t d1_12_seq;

static int d1_12_target(struct process *p)
{
	if (!p || !p->comm[0])
		return 0;
	return ir0_tty_smoke_active();
}

static void d1_12_hex_preview(const char *tag, const uint8_t *buf, size_t len)
{
	char line[256];
	size_t off;
	size_t i;

	off = (size_t)snprintf(line, sizeof(line), "%s", tag);
	for (i = 0; i < len && i < D1_12_PREVIEW && off + 4 < sizeof(line); i++)
		off += (size_t)snprintf(line + off, sizeof(line) - off, " %02x",
					buf[i]);
	klog_debug_fmt("D1.12", "%s", line);
}

static void d1_12_read_user_preview(struct process *p, uintptr_t buf,
				    size_t len)
{
	uint8_t tmp[D1_12_PREVIEW];
	size_t n;

	if (!p || !process_pgd(p) || !buf || len == 0)
		return;

	n = len;
	if (n > D1_12_PREVIEW)
		n = D1_12_PREVIEW;

	memset(tmp, 0, sizeof(tmp));
	if (copy_from_user(tmp, (const void *)buf, n) == 0)
		d1_12_hex_preview("[D1.12][USER_AFTER]", tmp, n);
	else
		klog_debug_fmt("D1.12", "[D1.12][USER_AFTER] copy_fail");
}

void d1_12_read_diag_syscall_pre(struct process *p, int fd, uintptr_t buf,
				 size_t count, uint64_t pc)
{
	if (!d1_12_target(p))
		return;

	d1_12_seq++;
	d1_12_last.valid = 1;
	d1_12_last.seq = d1_12_seq;
	d1_12_last.fd = fd;
	d1_12_last.user_buf = buf;
	d1_12_last.req_count = count;
	d1_12_last.ret = 0;
	d1_12_last.kcopy_len = 0;
	memset(d1_12_last.kpreview, 0, sizeof(d1_12_last.kpreview));
	memset(d1_12_last.upreview, 0, sizeof(d1_12_last.upreview));
	d1_12_last.pre_ret = 0;
	d1_12_last.pre_arg0 = process_syscall_arg(p, 0);
	d1_12_last.pre_arg1 = process_syscall_arg(p, 1);
	d1_12_last.pre_arg2 = process_syscall_arg(p, 2);
	d1_12_last.pre_pad = 0;
	d1_12_last.pre_arg4 = process_syscall_arg(p, 4);
	d1_12_last.pre_arg5 = process_syscall_arg(p, 5);
	d1_12_last.pre_arg3 = process_syscall_arg(p, 3);
	d1_12_last.pre_pc = process_syscall_ip(p);
	d1_12_last.pre_sp = process_syscall_sp(p);

	if (p->comm[0] == 's' && p->comm[1] == 'h' && p->comm[2] == '\0')
		d1_12_last_sh = d1_12_last;

	klog_debug_fmt("D1.12",
		       "[D1.12][READ_PRE] seq=%x pid=%x fd=%llx buf=%llx count=%llx rip=%llx frame_rdx=%llx",
		       (unsigned)d1_12_last.seq, (unsigned)(uint32_t)p->task.pid,
		       (unsigned long long)(uint64_t)fd,
		       (unsigned long long)(uint64_t)buf,
		       (unsigned long long)(uint64_t)count,
		       (unsigned long long)pc,
		       (unsigned long long)process_syscall_arg(p, 2));
}

void d1_12_read_diag_kcopy(int64_t ret, size_t req_count,
			   const char *kbuf, size_t kcopy_len)
{
	struct process *p = process_get_current();
	size_t n;

	if (!d1_12_target(p) || !d1_12_last.valid)
		return;

	d1_12_last.ret = ret;
	d1_12_last.kcopy_len = kcopy_len;
	if (kbuf && kcopy_len > 0)
	{
		n = kcopy_len;
		if (n > D1_12_PREVIEW)
			n = D1_12_PREVIEW;
		memcpy(d1_12_last.kpreview, kbuf, n);
	}

	klog_debug_fmt("D1.12",
		       "[D1.12][TTY_KCOPY] seq=%x req=%llx ret=%llx kcopy=%llx%s",
		       (unsigned)d1_12_last.seq,
		       (unsigned long long)(uint64_t)req_count,
		       (unsigned long long)ret,
		       (unsigned long long)kcopy_len,
		       kcopy_len > req_count ? " OVER_COUNT=1" : "");
	if (kbuf && kcopy_len > 0)
		d1_12_hex_preview("[D1.12][KERN_BUF]", (const uint8_t *)kbuf,
				  kcopy_len);
}

void d1_12_read_diag_syscall_post(struct process *p, int64_t ret)
{
	const char *magic_arg2 = "";
	const char *magic_ret = "";
	const char *arg2_neq = "";

	if (!d1_12_target(p) || !d1_12_last.valid)
		return;

	d1_12_last.post_ret = (uint64_t)ret;
	d1_12_last.restore_arg2 = process_syscall_arg(p, 2);
	d1_12_last.restore_arg1 = process_syscall_arg(p, 1);
	d1_12_last.restore_arg0 = process_syscall_arg(p, 0);
	if (ret > 0)
		d1_12_last.ret = ret;

	if (process_syscall_arg(p, 2) == D1_12_MAGIC_LEN)
		magic_arg2 = " RDX_EQ_1AF43F=1";
	if ((uint64_t)ret == D1_12_MAGIC_LEN)
		magic_ret = " RAX_EQ_1AF43F=1";
	if (process_syscall_arg(p, 2) != d1_12_last.req_count)
		arg2_neq = " RDX_NEQ_REQ=1";

	klog_debug_fmt("D1.12",
		       "[D1.12][READ_POST] seq=%x ret_rax=%llx restore_rdx=%llx restore_rsi=%llx restore_rdi=%llx%s%s%s",
		       (unsigned)d1_12_last.seq, (unsigned long long)(uint64_t)ret,
		       (unsigned long long)process_syscall_arg(p, 2),
		       (unsigned long long)process_syscall_arg(p, 1),
		       (unsigned long long)process_syscall_arg(p, 0),
		       magic_arg2, magic_ret, arg2_neq);

	if (ret > 0 && d1_12_last.user_buf)
		d1_12_read_user_preview(p, d1_12_last.user_buf, (size_t)ret);

	if (p->comm[0] == 's' && p->comm[1] == 'h' && p->comm[2] == '\0')
		d1_12_last_sh = d1_12_last;
}

void d1_12_read_diag_tty_line(size_t line_len, const char *kline,
			      size_t kline_len)
{
	struct process *p = process_get_current();

	if (!d1_12_target(p))
		return;

	d1_12_last.tty_line_len = line_len;

	klog_debug_fmt("D1.12", "[D1.12][TTY_LINE] len=%llx queued=%llx",
		       (unsigned long long)(uint64_t)line_len,
		       (unsigned long long)(uint64_t)kline_len);
	if (kline && kline_len > 0)
		d1_12_hex_preview("[D1.12][TTY_LINE_BYTES]", (const uint8_t *)kline,
				  kline_len);
}

void d1_12_read_diag_pf(struct process *p, uint64_t fault_pc, uint64_t arg2,
			uint64_t arg1, uint64_t arg0)
{
	if (!d1_12_target(p))
		return;

	klog_debug_fmt("D1.12", "=== [D1.12][PF_CORRELATE] ===");
	klog_debug_fmt("D1.12",
		       "[D1.12][PF] rip=%llx memcpy_rdx=%llx rsi=%llx rdi=%llx%s",
		       (unsigned long long)fault_pc, (unsigned long long)arg2,
		       (unsigned long long)arg1, (unsigned long long)arg0,
		       arg2 == D1_12_MAGIC_LEN ? " MEMCPY_RDX_IS_1AF43F=1" : "");

	if (d1_12_last.valid)
	{
		klog_debug_fmt("D1.12",
			       "[D1.12][LAST_READ] seq=%x fd=%llx req=%llx ret=%llx kcopy=%llx restore_rdx=%llx post_rax=%llx tty_line=%llx",
			       (unsigned)d1_12_last.seq,
			       (unsigned long long)(uint64_t)d1_12_last.fd,
			       (unsigned long long)d1_12_last.req_count,
			       (unsigned long long)d1_12_last.ret,
			       (unsigned long long)d1_12_last.kcopy_len,
			       (unsigned long long)d1_12_last.restore_arg2,
			       (unsigned long long)d1_12_last.post_ret,
			       (unsigned long long)d1_12_last.tty_line_len);

		if (arg2 == d1_12_last.restore_arg2)
			klog_debug_fmt("D1.12",
				       "[D1.12][MATCH] memcpy_rdx == read_restore_rdx");
		if (arg2 == (uint64_t)d1_12_last.req_count)
			klog_debug_fmt("D1.12",
				       "[D1.12][MATCH] memcpy_rdx == read_req_count");
		if (arg2 == (uint64_t)d1_12_last.ret)
			klog_debug_fmt("D1.12",
				       "[D1.12][MATCH] memcpy_rdx == read_ret_rax");
		if (d1_12_last.restore_arg2 == d1_12_last.req_count)
			klog_debug_fmt("D1.12",
				       "[D1.12][ABI] restore_rdx == req_count (preserved)");

		d1_12_hex_preview("[D1.12][LAST_KBUF]", d1_12_last.kpreview,
				  d1_12_last.kcopy_len);
	}
	else
		klog_debug_fmt("D1.12", "[D1.12][LAST_READ] none");

	if (d1_12_last_sh.valid)
	{
		klog_debug_fmt("D1.12",
			       "[D1.12][LAST_SH_READ] seq=%x fd=%llx req=%llx ret=%llx kcopy=%llx restore_rdx=%llx post_rax=%llx pre_rip=%llx%s",
			       (unsigned)d1_12_last_sh.seq,
			       (unsigned long long)(uint64_t)d1_12_last_sh.fd,
			       (unsigned long long)d1_12_last_sh.req_count,
			       (unsigned long long)d1_12_last_sh.ret,
			       (unsigned long long)d1_12_last_sh.kcopy_len,
			       (unsigned long long)d1_12_last_sh.restore_arg2,
			       (unsigned long long)d1_12_last_sh.post_ret,
			       (unsigned long long)d1_12_last_sh.pre_pc,
			       (d1_12_last_sh.post_ret == 0 &&
				d1_12_last_sh.kcopy_len == 0) ?
				       " SH_READ_INCOMPLETE=1" :
				       "");

		if (arg2 == d1_12_last_sh.restore_arg2)
			klog_debug_fmt("D1.12",
				       "[D1.12][SH_MATCH] memcpy_rdx == sh_restore_rdx");
		if (arg2 == (uint64_t)d1_12_last_sh.req_count)
			klog_debug_fmt("D1.12",
				       "[D1.12][SH_MATCH] memcpy_rdx == sh_req_count");
	}
	else
		klog_debug_fmt("D1.12", "[D1.12][LAST_SH_READ] none");

	klog_debug_fmt("D1.12", "=== [D1.12][PF_CORRELATE] end ===");
}
