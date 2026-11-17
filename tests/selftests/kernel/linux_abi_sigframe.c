/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: linux_abi_sigframe.c
 * Description: Where a signal handler frame goes and what leaving the handler
 *              without rt_sigreturn leaves behind. One case per divergence;
 *              each passes on Linux. Built with the conformance runner
 *              (make ir0-selftest-sigframe-abi-{x86,arm64}).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "conf.h"

#include <sys/mman.h>

#define ALT_STACK_SIZE 65536UL
/* The arm64 runner lives on a 16 KiB in-image stack; x86-64 on the real one. */
#if defined(__x86_64__)
#define DEEP_STACK_PAD 32768
#else
#define DEEP_STACK_PAD 4096
#endif

extern void rt_restore(void);

static volatile int g_hits;
static volatile unsigned long g_local_addr;
static void *g_jmp[5];

static void on_usr1_where(int sig)
{
	volatile char here = 0;

	(void)sig;
	g_hits++;
	g_local_addr = (unsigned long)&here;
}

static void on_usr1_longjmp(int sig)
{
	(void)sig;
	g_hits++;
	__builtin_longjmp(g_jmp, 1);
}

static long raise_usr1(void)
{
	return S2(SYS_kill, S0(SYS_getpid), SIGUSR1);
}

/*
 * sigframe_call_on_stack(top, fn): run fn with the stack pointer at top.
 * sigframe_red_zone_probe(): fill the area the ABI reserves next to the
 * stack pointer (x86-64: the 128-byte red zone below it; AArch64 has no
 * red zone, so the 32 bytes the probe owns at and above it), send itself
 * SIGUSR1 and return 0 when that area survived the delivery.
 */
long sigframe_call_on_stack(void *top, long (*fn)(void));
long sigframe_red_zone_probe(void);

#if defined(__x86_64__)
__asm__(".text\n"
	".globl sigframe_call_on_stack\n"
	".type sigframe_call_on_stack, @function\n"
	"sigframe_call_on_stack:\n"
	"	push	%rbp\n"
	"	mov	%rsp, %rbp\n"
	"	mov	%rdi, %rsp\n"
	"	call	*%rsi\n"
	"	mov	%rbp, %rsp\n"
	"	pop	%rbp\n"
	"	ret\n"
	".globl sigframe_red_zone_probe\n"
	".type sigframe_red_zone_probe, @function\n"
	"sigframe_red_zone_probe:\n"
	"	movabs	$0x5a5a5a5a5a5a5a5a, %rdx\n"
	"	lea	-128(%rsp), %rcx\n"
	"	mov	$16, %r8d\n"
	"1:	mov	%rdx, (%rcx)\n"
	"	add	$8, %rcx\n"
	"	dec	%r8d\n"
	"	jnz	1b\n"
	"	mov	$39, %eax\n"
	"	syscall\n"
	"	mov	%rax, %rdi\n"
	"	mov	$10, %esi\n"
	"	mov	$62, %eax\n"
	"	syscall\n"
	"	movabs	$0x5a5a5a5a5a5a5a5a, %rdx\n"
	"	lea	-128(%rsp), %rcx\n"
	"	mov	$16, %r8d\n"
	"	xor	%eax, %eax\n"
	"2:	cmp	%rdx, (%rcx)\n"
	"	jne	3f\n"
	"	add	$8, %rcx\n"
	"	dec	%r8d\n"
	"	jnz	2b\n"
	"	ret\n"
	"3:	mov	$1, %eax\n"
	"	ret\n");
#elif defined(__aarch64__)
__asm__(".text\n"
	".globl sigframe_call_on_stack\n"
	".type sigframe_call_on_stack, %function\n"
	"sigframe_call_on_stack:\n"
	"	stp	x29, x30, [sp, #-16]!\n"
	"	mov	x29, sp\n"
	"	mov	sp, x0\n"
	"	blr	x1\n"
	"	mov	sp, x29\n"
	"	ldp	x29, x30, [sp], #16\n"
	"	ret\n"
	".globl sigframe_red_zone_probe\n"
	".type sigframe_red_zone_probe, %function\n"
	"sigframe_red_zone_probe:\n"
	"	sub	sp, sp, #32\n"
	"	movz	x9, #0x5a5a\n"
	"	movk	x9, #0x5a5a, lsl #16\n"
	"	movk	x9, #0x5a5a, lsl #32\n"
	"	movk	x9, #0x5a5a, lsl #48\n"
	"	stp	x9, x9, [sp]\n"
	"	stp	x9, x9, [sp, #16]\n"
	"	mov	x8, #172\n"
	"	svc	#0\n"
	"	mov	x1, #10\n"
	"	mov	x8, #129\n"
	"	svc	#0\n"
	"	mov	x0, #1\n"
	"	ldp	x10, x11, [sp]\n"
	"	cmp	x10, x9\n"
	"	b.ne	1f\n"
	"	cmp	x11, x9\n"
	"	b.ne	1f\n"
	"	ldp	x10, x11, [sp, #16]\n"
	"	cmp	x10, x9\n"
	"	b.ne	1f\n"
	"	cmp	x11, x9\n"
	"	b.ne	1f\n"
	"	mov	x0, #0\n"
	"1:	add	sp, sp, #32\n"
	"	ret\n");
#else
#error "linux_abi_sigframe: unsupported ISA"
#endif

/*
 * signal(7): a handler that leaves with longjmp(3) skips rt_sigreturn, so
 * the mask the kernel installed for it (the signal plus sa_mask) stays in
 * force until the program changes it; only siglongjmp(3) restores a saved
 * mask, and it does so from userspace. Linux has no kernel hook for this.
 */
int sig_longjmp_keeps_handler_mask(void)
{
	unsigned long want = SIGBIT(SIGUSR1) | SIGBIT(SIGUSR2);
	struct ksigaction act;
	unsigned long none = 0;
	unsigned long cur = 0;

	act.handler = on_usr1_longjmp;
	act.flags = SA_RESTORER;
	act.restorer = rt_restore;
	act.mask = SIGBIT(SIGUSR2);
	EXPECT(S4(SYS_rt_sigaction, SIGUSR1, &act, 0, sizeof(act.mask)), 0);
	EXPECT(S4(SYS_rt_sigprocmask, SIG_SETMASK, &none, 0, 8), 0);
	if (__builtin_setjmp(g_jmp) == 0)
	{
		(void)raise_usr1();
		return CONF_FAIL;
	}
	EXPECT(g_hits, 1);
	EXPECT(S4(SYS_rt_sigprocmask, SIG_BLOCK, 0, &cur, 8), 0);
	EXPECT(cur & want, want);
	return CONF_PASS;
}

/*
 * arch/x86/kernel/signal.c and arch/arm64/kernel/signal.c get_sigframe():
 * without SA_ONSTACK the frame goes just below the interrupted stack
 * pointer, whatever mapping it points into (a thread stack from mmap(2)
 * here), never somewhere else in the address space.
 */
int sig_frame_on_interrupted_stack(void)
{
	long base;

	OK(base, S6(SYS_mmap, 0, ALT_STACK_SIZE, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
	EXPECT(rt_sigaction(SIGUSR1, on_usr1_where, 0, 0), 0);
	EXPECT(sigframe_call_on_stack((void *)(base + ALT_STACK_SIZE),
				      raise_usr1), 0);
	EXPECT(g_hits, 1);
	CHECK(g_local_addr >= (unsigned long)base &&
	      g_local_addr < (unsigned long)base + ALT_STACK_SIZE);
	return CONF_PASS;
}

/* Run the probe well below the top of the stack, as deep code would. */
static long red_zone_probe_deep(void)
{
	volatile char pad[DEEP_STACK_PAD];
	long r;

	pad[0] = 0;
	r = sigframe_red_zone_probe();
	pad[DEEP_STACK_PAD - 1] = (char)r;
	return r;
}

/*
 * arch/x86/kernel/signal.c get_sigframe() skips the 128-byte red zone the
 * x86-64 psABI gives a leaf function below its stack pointer; the frame
 * never overlaps memory the interrupted code may still use.
 */
int sig_frame_spares_red_zone(void)
{
	EXPECT(rt_sigaction(SIGUSR1, on_usr1_where, 0, 0), 0);
	EXPECT(red_zone_probe_deep(), 0);
	EXPECT(g_hits, 1);
	return CONF_PASS;
}
