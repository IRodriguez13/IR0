/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: fpsimd.c
 * Description: The FP/SIMD registers are per-task state, as on Linux (x86
 *              fpu, arm64 fpsimd): two processes that sleep with distinct
 *              values in every vector register and in the FP control
 *              register find their own values when they run again, a fork
 *              child starts with its parent's values, and a signal handler
 *              that overwrites them returns to code that still sees the
 *              interrupted values. Each check loads the registers, enters
 *              the kernel and reads them back inside one asm statement, so
 *              the compiler cannot touch them in between.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#endif

#include "ir0/ksft.h"

#ifndef IR0_SELFTEST_NOLIBC
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#endif

#define ROUNDS 32
#define SLEEP_NS 2000000L
#define CHILD_OK 0
#define CHILD_MISMATCH 1
#define CHILD_SETUP 2

#if defined(__aarch64__)
#define FP_BYTES (32 * 16)
#define NR_NANOSLEEP 101
#define NR_KILL 129
#define NR_CLONE 220
/* FPCR.RMode (bits 23:22) per owner; FPSR cumulative exception flags. */
#define CTRL_PARENT 0x400000UL
#define CTRL_CHILD 0x800000UL
#define CTRL_HANDLER 0xc00000UL
#define STATUS_PARENT 0x01UL
#define STATUS_CHILD 0x02UL
#define STATUS_HANDLER 0x1fUL
#else
#define FP_BYTES (16 * 16)
#define NR_NANOSLEEP 35
#define NR_KILL 62
#define NR_CLONE 56
/* MXCSR.RC (bits 14:13) per owner, all exceptions masked. */
#define CTRL_PARENT 0x3f80UL
#define CTRL_CHILD 0x5f80UL
#define CTRL_HANDLER 0x7f80UL
#define STATUS_PARENT 0
#define STATUS_CHILD 0
#define STATUS_HANDLER 0
#endif

#define CLONE_SIGCHLD 17

struct fp_image
{
	unsigned char v[FP_BYTES] __attribute__((aligned(16)));
	unsigned long ctrl;
	unsigned long status;
};

static struct fp_image handler_image;
static volatile int handler_runs;

static void fp_fill(struct fp_image *img, unsigned seed, unsigned long ctrl,
		    unsigned long status)
{
	unsigned i;

	for (i = 0; i < FP_BYTES; i++)
		img->v[i] = (unsigned char)(seed * 0x3b + i * 7 + 1);
	img->ctrl = ctrl;
	img->status = status;
}

#if defined(__aarch64__)
#define FP_LOAD(in) \
	"ldp q0, q1, [" in ", #0]\n\t" \
	"ldp q2, q3, [" in ", #32]\n\t" \
	"ldp q4, q5, [" in ", #64]\n\t" \
	"ldp q6, q7, [" in ", #96]\n\t" \
	"ldp q8, q9, [" in ", #128]\n\t" \
	"ldp q10, q11, [" in ", #160]\n\t" \
	"ldp q12, q13, [" in ", #192]\n\t" \
	"ldp q14, q15, [" in ", #224]\n\t" \
	"ldp q16, q17, [" in ", #256]\n\t" \
	"ldp q18, q19, [" in ", #288]\n\t" \
	"ldp q20, q21, [" in ", #320]\n\t" \
	"ldp q22, q23, [" in ", #352]\n\t" \
	"ldp q24, q25, [" in ", #384]\n\t" \
	"ldp q26, q27, [" in ", #416]\n\t" \
	"ldp q28, q29, [" in ", #448]\n\t" \
	"ldp q30, q31, [" in ", #480]\n\t"
#define FP_STORE(out) \
	"stp q0, q1, [" out ", #0]\n\t" \
	"stp q2, q3, [" out ", #32]\n\t" \
	"stp q4, q5, [" out ", #64]\n\t" \
	"stp q6, q7, [" out ", #96]\n\t" \
	"stp q8, q9, [" out ", #128]\n\t" \
	"stp q10, q11, [" out ", #160]\n\t" \
	"stp q12, q13, [" out ", #192]\n\t" \
	"stp q14, q15, [" out ", #224]\n\t" \
	"stp q16, q17, [" out ", #256]\n\t" \
	"stp q18, q19, [" out ", #288]\n\t" \
	"stp q20, q21, [" out ", #320]\n\t" \
	"stp q22, q23, [" out ", #352]\n\t" \
	"stp q24, q25, [" out ", #384]\n\t" \
	"stp q26, q27, [" out ", #416]\n\t" \
	"stp q28, q29, [" out ", #448]\n\t" \
	"stp q30, q31, [" out ", #480]\n\t"
#define FP_CLOBBERS \
	"v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", "v10", \
	"v11", "v12", "v13", "v14", "v15", "v16", "v17", "v18", "v19", \
	"v20", "v21", "v22", "v23", "v24", "v25", "v26", "v27", "v28", \
	"v29", "v30", "v31"

/*
 * Load @in into v0..v31, FPCR and FPSR, make syscall @nr, then store the
 * registers to @out. Returns the syscall result.
 */
static long fp_syscall(const struct fp_image *in, struct fp_image *out,
		       long nr, long a0, long a1)
{
	register long x8 __asm__("x8") = nr;
	register long x0 __asm__("x0") = a0;
	register long x1 __asm__("x1") = a1;
	register long x2 __asm__("x2") = 0;
	register long x3 __asm__("x3") = 0;
	register long x4 __asm__("x4") = 0;
	unsigned long ctrl;
	unsigned long status;

	__asm__ volatile(
		"msr fpcr, %[ci]\n\t"
		"msr fpsr, %[si]\n\t"
		FP_LOAD("%[in]")
		"svc #0\n\t"
		FP_STORE("%[out]")
		"mrs %[co], fpcr\n\t"
		"mrs %[so], fpsr\n\t"
		: "+r"(x0), [co] "=&r"(ctrl), [so] "=&r"(status)
		: "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4),
		  [in] "r"(in->v), [out] "r"(out->v),
		  [ci] "r"(in->ctrl), [si] "r"(in->status)
		: "memory", "cc", FP_CLOBBERS);
	out->ctrl = ctrl;
	out->status = status;
	return x0;
}

static void fp_load(const struct fp_image *in)
{
	__asm__ volatile(
		"msr fpcr, %[ci]\n\t"
		"msr fpsr, %[si]\n\t"
		FP_LOAD("%[in]")
		:
		: [in] "r"(in->v), [ci] "r"(in->ctrl), [si] "r"(in->status)
		: "memory", FP_CLOBBERS);
}
#else
#define FP_LOAD(in) \
	"movdqa 0(" in "), %%xmm0\n\t" \
	"movdqa 16(" in "), %%xmm1\n\t" \
	"movdqa 32(" in "), %%xmm2\n\t" \
	"movdqa 48(" in "), %%xmm3\n\t" \
	"movdqa 64(" in "), %%xmm4\n\t" \
	"movdqa 80(" in "), %%xmm5\n\t" \
	"movdqa 96(" in "), %%xmm6\n\t" \
	"movdqa 112(" in "), %%xmm7\n\t" \
	"movdqa 128(" in "), %%xmm8\n\t" \
	"movdqa 144(" in "), %%xmm9\n\t" \
	"movdqa 160(" in "), %%xmm10\n\t" \
	"movdqa 176(" in "), %%xmm11\n\t" \
	"movdqa 192(" in "), %%xmm12\n\t" \
	"movdqa 208(" in "), %%xmm13\n\t" \
	"movdqa 224(" in "), %%xmm14\n\t" \
	"movdqa 240(" in "), %%xmm15\n\t"
#define FP_STORE(out) \
	"movdqa %%xmm0, 0(" out ")\n\t" \
	"movdqa %%xmm1, 16(" out ")\n\t" \
	"movdqa %%xmm2, 32(" out ")\n\t" \
	"movdqa %%xmm3, 48(" out ")\n\t" \
	"movdqa %%xmm4, 64(" out ")\n\t" \
	"movdqa %%xmm5, 80(" out ")\n\t" \
	"movdqa %%xmm6, 96(" out ")\n\t" \
	"movdqa %%xmm7, 112(" out ")\n\t" \
	"movdqa %%xmm8, 128(" out ")\n\t" \
	"movdqa %%xmm9, 144(" out ")\n\t" \
	"movdqa %%xmm10, 160(" out ")\n\t" \
	"movdqa %%xmm11, 176(" out ")\n\t" \
	"movdqa %%xmm12, 192(" out ")\n\t" \
	"movdqa %%xmm13, 208(" out ")\n\t" \
	"movdqa %%xmm14, 224(" out ")\n\t" \
	"movdqa %%xmm15, 240(" out ")\n\t"
#define FP_CLOBBERS \
	"xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7", \
	"xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15"

/*
 * Load @in into xmm0..xmm15 and MXCSR, make syscall @nr, then store the
 * registers to @out. Returns the syscall result.
 */
static long fp_syscall(const struct fp_image *in, struct fp_image *out,
		       long nr, long a0, long a1)
{
	register long r10 __asm__("r10") = 0;
	register long r8 __asm__("r8") = 0;
	unsigned int ctrl_in = (unsigned int)in->ctrl;
	unsigned int ctrl_out = 0;
	long ret;

	__asm__ volatile(
		"ldmxcsr %[ci]\n\t"
		FP_LOAD("%[in]")
		"syscall\n\t"
		FP_STORE("%[out]")
		"stmxcsr %[co]\n\t"
		: "=a"(ret), [co] "=m"(ctrl_out)
		: "a"(nr), "D"(a0), "S"(a1), "d"(0L), "r"(r10), "r"(r8),
		  [in] "r"(in->v), [out] "r"(out->v), [ci] "m"(ctrl_in)
		: "rcx", "r11", "memory", "cc", FP_CLOBBERS);
	out->ctrl = ctrl_out;
	out->status = 0;
	return ret;
}

static void fp_load(const struct fp_image *in)
{
	unsigned int ctrl_in = (unsigned int)in->ctrl;

	__asm__ volatile(
		"ldmxcsr %[ci]\n\t"
		FP_LOAD("%[in]")
		:
		: [in] "r"(in->v), [ci] "m"(ctrl_in)
		: "memory", FP_CLOBBERS);
}
#endif

static int fp_equal(const struct fp_image *a, const struct fp_image *b)
{
	unsigned i;

	for (i = 0; i < FP_BYTES; i++)
	{
		if (a->v[i] != b->v[i])
		{
			ksft_print_num("fp_byte", (long)i);
			return 0;
		}
	}
	if (a->ctrl != b->ctrl)
	{
		ksft_print_num("fp_ctrl", (long)b->ctrl);
		return 0;
	}
	if (a->status != b->status)
	{
		ksft_print_num("fp_status", (long)b->status);
		return 0;
	}
	return 1;
}

/* Sleep ROUNDS times with @img in the registers; 1 if it always survived. */
static int sleep_rounds(const struct fp_image *img)
{
	static const struct timespec nap = { 0, SLEEP_NS };
	struct fp_image seen;
	int round;

	for (round = 0; round < ROUNDS; round++)
	{
		if (fp_syscall(img, &seen, NR_NANOSLEEP, (long)&nap, 0) != 0)
			return -1;
		if (!fp_equal(img, &seen))
			return 0;
	}
	return 1;
}

static int wait_child(int pid)
{
	int status = 0;

	if (wait4(pid, &status, 0, 0) != pid)
	{
		ksft_print_num("wait4_errno", errno);
		return -1;
	}
	return status;
}

/*
 * Parent and child sleep in turns with distinct register values; each must
 * get its own values back after every switch.
 */
static void test_switch_keeps_state(void)
{
	struct fp_image parent_img;
	struct fp_image child_img;
	int parent_ok;
	int status;
	int pid;

	fp_fill(&parent_img, 1, CTRL_PARENT, STATUS_PARENT);
	fp_fill(&child_img, 2, CTRL_CHILD, STATUS_CHILD);
	pid = fork();
	if (pid < 0)
	{
		ksft_print_num("fork_errno", errno);
		ksft_test_result(0, "switch_keeps_parent_fpsimd");
		ksft_test_result(0, "switch_keeps_child_fpsimd");
		return;
	}
	if (pid == 0)
	{
		int r = sleep_rounds(&child_img);

		_exit(r == 1 ? CHILD_OK : (r == 0 ? CHILD_MISMATCH : CHILD_SETUP));
	}
	parent_ok = sleep_rounds(&parent_img);
	status = wait_child(pid);
	ksft_test_result(parent_ok == 1, "switch_keeps_parent_fpsimd");
	if (status < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != CHILD_OK)
		ksft_print_num("child_status", status);
	ksft_test_result(status >= 0 && WIFEXITED(status) &&
			 WEXITSTATUS(status) == CHILD_OK,
			 "switch_keeps_child_fpsimd");
}

/* A fork child starts with the parent's registers at the fork. */
static void test_fork_inherits_state(void)
{
	struct fp_image img;
	struct fp_image seen;
	long pid;
	int status;

	fp_fill(&img, 3, CTRL_PARENT, STATUS_PARENT);
	pid = fp_syscall(&img, &seen, NR_CLONE, CLONE_SIGCHLD, 0);
	if (pid == 0)
		_exit(fp_equal(&img, &seen) ? CHILD_OK : CHILD_MISMATCH);
	if (pid < 0)
	{
		ksft_print_num("clone_ret", pid);
		ksft_test_result(0, "fork_child_inherits_fpsimd");
		return;
	}
	status = wait_child((int)pid);
	if (status < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != CHILD_OK)
		ksft_print_num("child_status", status);
	ksft_test_result(status >= 0 && WIFEXITED(status) &&
			 WEXITSTATUS(status) == CHILD_OK,
			 "fork_child_inherits_fpsimd");
}

static void clobber_fpsimd(int sig)
{
	(void)sig;
	fp_load(&handler_image);
	handler_runs++;
}

/*
 * A handler that overwrites every FP/SIMD register returns through
 * rt_sigreturn to code that still sees the interrupted values.
 */
static void test_signal_restores_state(void)
{
	struct sigaction sa = { 0 };
	struct fp_image img;
	struct fp_image seen;
	int ok;

	fp_fill(&handler_image, 4, CTRL_HANDLER, STATUS_HANDLER);
	fp_fill(&img, 5, CTRL_PARENT, STATUS_PARENT);
	sa.sa_handler = clobber_fpsimd;
	if (sigaction(SIGUSR1, &sa, 0) != 0)
	{
		ksft_print_num("sigaction_errno", errno);
		ksft_test_result(0, "signal_handler_keeps_fpsimd");
		return;
	}
	if (fp_syscall(&img, &seen, NR_KILL, getpid(), SIGUSR1) != 0)
	{
		ksft_test_result(0, "signal_handler_keeps_fpsimd");
		return;
	}
	ok = handler_runs == 1;
	if (!ok)
		ksft_print_num("handler_runs", handler_runs);
	ksft_test_result(ok && fp_equal(&img, &seen),
			 "signal_handler_keeps_fpsimd");
}

int main(void)
{
	ksft_set_plan(4);
	test_switch_keeps_state();
	test_fork_inherits_state();
	test_signal_restores_state();
	return ksft_exit();
}
