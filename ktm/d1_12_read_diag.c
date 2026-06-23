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
#include <ir0/ash_smoke.h>
#include <ir0/copy_user.h>
#include <ir0/process.h>
#include <ir0/serial_io.h>
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
	uint64_t pre_rax;
	uint64_t pre_rdi;
	uint64_t pre_rsi;
	uint64_t pre_rdx;
	uint64_t pre_rcx;
	uint64_t pre_r8;
	uint64_t pre_r9;
	uint64_t pre_r10;
	uint64_t pre_rip;
	uint64_t pre_rsp;
	uint64_t post_rax;
	uint64_t restore_rdx;
	uint64_t restore_rsi;
	uint64_t restore_rdi;
	size_t   tty_line_len;
};

static struct d1_12_last_read d1_12_last;
static struct d1_12_last_read d1_12_last_sh;
static uint32_t d1_12_seq;

static int d1_12_target(struct process *p)
{
	if (!p || !p->comm[0])
		return 0;
	if (p->comm[0] == 's' && p->comm[1] == 'h' && p->comm[2] == '\0')
		return 1;
	return ir0_ash_smoke_active();
}

static void d1_12_hex_preview(const char *tag, const uint8_t *buf, size_t len)
{
	size_t i;
	static const char hex[] = "0123456789ABCDEF";

	serial_print(tag);
	for (i = 0; i < len && i < D1_12_PREVIEW; i++)
	{
		serial_print(" ");
		{
			char out[3];

			out[0] = hex[(buf[i] >> 4) & 0xF];
			out[1] = hex[buf[i] & 0xF];
			out[2] = '\0';
			serial_print(out);
		}
	}
	serial_print("\n");
}

static void d1_12_read_user_preview(struct process *p, uintptr_t buf,
				    size_t len)
{
	uint8_t tmp[D1_12_PREVIEW];
	size_t n;

	if (!p || !p->page_directory || !buf || len == 0)
		return;

	n = len;
	if (n > D1_12_PREVIEW)
		n = D1_12_PREVIEW;

	memset(tmp, 0, sizeof(tmp));
	if (copy_from_user(tmp, (const void *)buf, n) == 0)
		d1_12_hex_preview("[D1.12][USER_AFTER]", tmp, n);
	else
		serial_print("[D1.12][USER_AFTER] copy_fail\n");
}

void d1_12_read_diag_syscall_pre(struct process *p, int fd, uintptr_t buf,
				 size_t count, uint64_t rip)
{
	const syscall_user_frame_t *sf;

	if (!d1_12_target(p))
		return;

	sf = &p->syscall_frame;
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
	d1_12_last.pre_rax = 0;
	d1_12_last.pre_rdi = sf->rdi;
	d1_12_last.pre_rsi = sf->rsi;
	d1_12_last.pre_rdx = sf->rdx;
	d1_12_last.pre_rcx = 0;
	d1_12_last.pre_r8 = sf->r8;
	d1_12_last.pre_r9 = sf->r9;
	d1_12_last.pre_r10 = sf->r10;
	d1_12_last.pre_rip = sf->rip;
	d1_12_last.pre_rsp = sf->rsp;

	if (p->comm[0] == 's' && p->comm[1] == 'h' && p->comm[2] == '\0')
		d1_12_last_sh = d1_12_last;

	serial_print("\n[D1.12][READ_PRE] seq=");
	serial_print_hex32(d1_12_last.seq);
	serial_print(" pid=");
	serial_print_hex32((uint32_t)p->task.pid);
	serial_print(" fd=");
	serial_print_hex64((uint64_t)fd);
	serial_print(" buf=");
	serial_print_hex64((uint64_t)buf);
	serial_print(" count=");
	serial_print_hex64((uint64_t)count);
	serial_print(" rip=");
	serial_print_hex64(rip);
	serial_print(" frame_rdx=");
	serial_print_hex64(sf->rdx);
	serial_print("\n");
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

	serial_print("[D1.12][TTY_KCOPY] seq=");
	serial_print_hex32(d1_12_last.seq);
	serial_print(" req=");
	serial_print_hex64((uint64_t)req_count);
	serial_print(" ret=");
	serial_print_hex64((uint64_t)ret);
	serial_print(" kcopy=");
	serial_print_hex64((uint64_t)kcopy_len);
	if (kcopy_len > req_count)
		serial_print(" OVER_COUNT=1");
	serial_print("\n");
	if (kbuf && kcopy_len > 0)
		d1_12_hex_preview("[D1.12][KERN_BUF]", (const uint8_t *)kbuf,
				  kcopy_len);
}

void d1_12_read_diag_syscall_post(struct process *p, int64_t ret)
{
	const syscall_user_frame_t *sf;

	if (!d1_12_target(p) || !d1_12_last.valid)
		return;

	sf = &p->syscall_frame;
	d1_12_last.post_rax = (uint64_t)ret;
	d1_12_last.restore_rdx = sf->rdx;
	d1_12_last.restore_rsi = sf->rsi;
	d1_12_last.restore_rdi = sf->rdi;
	if (ret > 0)
		d1_12_last.ret = ret;

	serial_print("[D1.12][READ_POST] seq=");
	serial_print_hex32(d1_12_last.seq);
	serial_print(" ret_rax=");
	serial_print_hex64((uint64_t)ret);
	serial_print(" restore_rdx=");
	serial_print_hex64(sf->rdx);
	serial_print(" restore_rsi=");
	serial_print_hex64(sf->rsi);
	serial_print(" restore_rdi=");
	serial_print_hex64(sf->rdi);
	if (sf->rdx == D1_12_MAGIC_LEN)
		serial_print(" RDX_EQ_1AF43F=1");
	if ((uint64_t)ret == D1_12_MAGIC_LEN)
		serial_print(" RAX_EQ_1AF43F=1");
	if (sf->rdx != d1_12_last.req_count)
	{
		serial_print(" RDX_NEQ_REQ=1");
	}
	serial_print("\n");

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

	serial_print("[D1.12][TTY_LINE] len=");
	serial_print_hex64((uint64_t)line_len);
	serial_print(" queued=");
	serial_print_hex64((uint64_t)kline_len);
	serial_print("\n");
	if (kline && kline_len > 0)
		d1_12_hex_preview("[D1.12][TTY_LINE_BYTES]", (const uint8_t *)kline,
				  kline_len);
}

void d1_12_read_diag_pf(struct process *p, uint64_t fault_rip, uint64_t rdx,
			uint64_t rsi, uint64_t rdi)
{
	if (!d1_12_target(p))
		return;

	serial_print("\n=== [D1.12][PF_CORRELATE] ===\n");
	serial_print("[D1.12][PF] rip=");
	serial_print_hex64(fault_rip);
	serial_print(" memcpy_rdx=");
	serial_print_hex64(rdx);
	serial_print(" rsi=");
	serial_print_hex64(rsi);
	serial_print(" rdi=");
	serial_print_hex64(rdi);
	if (rdx == D1_12_MAGIC_LEN)
		serial_print(" MEMCPY_RDX_IS_1AF43F=1");
	serial_print("\n");

	if (d1_12_last.valid)
	{
		serial_print("[D1.12][LAST_READ] seq=");
		serial_print_hex32(d1_12_last.seq);
		serial_print(" fd=");
		serial_print_hex64((uint64_t)d1_12_last.fd);
		serial_print(" req=");
		serial_print_hex64((uint64_t)d1_12_last.req_count);
		serial_print(" ret=");
		serial_print_hex64((uint64_t)d1_12_last.ret);
		serial_print(" kcopy=");
		serial_print_hex64((uint64_t)d1_12_last.kcopy_len);
		serial_print(" restore_rdx=");
		serial_print_hex64(d1_12_last.restore_rdx);
		serial_print(" post_rax=");
		serial_print_hex64(d1_12_last.post_rax);
		serial_print(" tty_line=");
		serial_print_hex64((uint64_t)d1_12_last.tty_line_len);
		serial_print("\n");

		if (rdx == d1_12_last.restore_rdx)
			serial_print("[D1.12][MATCH] memcpy_rdx == read_restore_rdx\n");
		if (rdx == (uint64_t)d1_12_last.req_count)
			serial_print("[D1.12][MATCH] memcpy_rdx == read_req_count\n");
		if (rdx == (uint64_t)d1_12_last.ret)
			serial_print("[D1.12][MATCH] memcpy_rdx == read_ret_rax\n");
		if (d1_12_last.restore_rdx == d1_12_last.req_count)
			serial_print("[D1.12][ABI] restore_rdx == req_count (preserved)\n");

		d1_12_hex_preview("[D1.12][LAST_KBUF]", d1_12_last.kpreview,
				  d1_12_last.kcopy_len);
	}
	else
		serial_print("[D1.12][LAST_READ] none\n");

	if (d1_12_last_sh.valid)
	{
		serial_print("[D1.12][LAST_SH_READ] seq=");
		serial_print_hex32(d1_12_last_sh.seq);
		serial_print(" fd=");
		serial_print_hex64((uint64_t)d1_12_last_sh.fd);
		serial_print(" req=");
		serial_print_hex64((uint64_t)d1_12_last_sh.req_count);
		serial_print(" ret=");
		serial_print_hex64((uint64_t)d1_12_last_sh.ret);
		serial_print(" kcopy=");
		serial_print_hex64((uint64_t)d1_12_last_sh.kcopy_len);
		serial_print(" restore_rdx=");
		serial_print_hex64(d1_12_last_sh.restore_rdx);
		serial_print(" post_rax=");
		serial_print_hex64(d1_12_last_sh.post_rax);
		serial_print(" pre_rip=");
		serial_print_hex64(d1_12_last_sh.pre_rip);
		if (d1_12_last_sh.post_rax == 0 && d1_12_last_sh.kcopy_len == 0)
			serial_print(" SH_READ_INCOMPLETE=1");
		serial_print("\n");

		if (rdx == d1_12_last_sh.restore_rdx)
			serial_print("[D1.12][SH_MATCH] memcpy_rdx == sh_restore_rdx\n");
		if (rdx == (uint64_t)d1_12_last_sh.req_count)
			serial_print("[D1.12][SH_MATCH] memcpy_rdx == sh_req_count\n");
	}
	else
		serial_print("[D1.12][LAST_SH_READ] none\n");

	serial_print("=== [D1.12][PF_CORRELATE] end ===\n\n");
}
