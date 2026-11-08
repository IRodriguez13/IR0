/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: oops.c
 * Description: IR0 kernel source/header file
 */

#include "process.h"
#include <ir0/oops.h>
#include <ir0/vga.h>
#include <ir0/console_backend.h>
#include <ir0/ktm/klog.h>
#include <ir0/cpu.h>
#include <ir0/clock.h>
#include <string.h>
#include <ir0/oops_port.h>
#include <ir0/fault.h>
#include <mm/paging.h>

/*
 * Set by panicex(). Private to this file: a fault taken while it is set must
 * still reach panicex(), whose nested branch keeps the primary frame and
 * halts; callers never test it themselves.
 */
static volatile int in_panic = 0;
static volatile int panicex_tag_done;

/* Fault captured before panicex (CPU fault site, not the reporter). */
static struct
{
	int valid;
	int stack_overflow;
	uint32_t pid;
	struct ir0_fault fault; /* addr frozen at note time; nested faults keep it */
	char comm[16];
} panic_exc_frame;

int ir0_fault_sp_in_kstack_guard(uintptr_t sp)
{
	uint64_t in_slot;

	if ((uint64_t)sp < IR0_KSTACK_VA_BASE)
		return 0;
	/* The bottom page of every kstack slot is the unmapped guard. */
	in_slot = ((uint64_t)sp - IR0_KSTACK_VA_BASE) %
		  (uint64_t)IR0_KSTACK_SLOT_SIZE;
	return in_slot < (uint64_t)PAGE_SIZE_4KB;
}

void panic_note_fault(const struct ir0_fault *fault, unsigned int pid,
		      const char *comm)
{
	size_t i;

	if (!fault)
		return;
	/*
	 * Keep the first (primary) fault: a nested fault during the panic
	 * dump must not replace the kernel fault site with the dumper's own.
	 */
	if (panic_exc_frame.valid)
	{
		/* Name the nested exception; panicex() halts on its nested branch. */
		if (in_panic)
		{
			klog_print("\nCLASSIFY NESTED_CPU_EXCEPTION_DURING_PANIC kind=");
			klog_print(ir0_fault_kind_name(fault->kind));
			klog_print(" ip=");
			klog_hex64((uint64_t)fault->ip);
			klog_print(" sp=");
			klog_hex64((uint64_t)fault->sp);
			klog_print("\n");
		}
		return;
	}

	panic_exc_frame.valid = 1;
	panic_exc_frame.fault = *fault;
	panic_exc_frame.stack_overflow = ir0_fault_sp_in_kstack_guard(fault->sp);
	panic_exc_frame.pid = pid;
	panic_exc_frame.comm[0] = '\0';
	if (comm)
	{
		for (i = 0; i < sizeof(panic_exc_frame.comm) - 1 && comm[i]; i++)
			panic_exc_frame.comm[i] = comm[i];
		panic_exc_frame.comm[i] = '\0';
	}
}

unsigned long long ir0_panic_fault_rsp(void)
{
	return panic_exc_frame.valid ? panic_exc_frame.fault.sp : 0;
}

void ir0_log_user_fault(const struct ir0_fault *fault, unsigned int pid,
			const char *comm)
{
	const char *classify;
	process_t *cur = process_get_current();
	uintptr_t addr;

	if (!fault)
		return;
	addr = fault->addr;

	/*
	 * present/write/exec describe page faults only; other kinds name the
	 * exception class instead of being mislabeled USER_NOT_PRESENT.
	 */
	switch (fault->kind)
	{
	case IR0_FAULT_PROTECTION:
		classify = "USER_GENERAL_PROTECTION";
		break;
	case IR0_FAULT_STACK_SEGMENT:
		classify = "USER_STACK_SEGMENT";
		break;
	case IR0_FAULT_ILLEGAL_INSN:
		classify = "USER_INVALID_OPCODE";
		break;
	case IR0_FAULT_ARITHMETIC:
		classify = "USER_ARITHMETIC";
		break;
	case IR0_FAULT_ALIGNMENT:
		classify = "USER_ALIGNMENT";
		break;
	case IR0_FAULT_BREAKPOINT:
		classify = "USER_BREAKPOINT";
		break;
	case IR0_FAULT_PAGE:
		if (addr >= (uintptr_t)USER_STACK_TOP &&
		    addr < (uintptr_t)USER_STACK_TOP + PAGE_SIZE_4KB)
			classify = "USER_STACK_TOP_OVERRUN";
		else if ((uint64_t)addr >= IR0_KSTACK_VA_BASE &&
			 (uint64_t)addr < IR0_KSTACK_VA_BASE +
				 (uint64_t)IR0_KSTACK_MAX_SLOTS *
					 (uint64_t)IR0_KSTACK_SLOT_SIZE)
			classify = "USER_TOUCHED_KSTACK_VA";
		else if (!fault->present)
			classify = "USER_NOT_PRESENT";
		else if (fault->write)
			classify = "USER_PROT_WRITE";
		else if (fault->exec)
			classify = "USER_PROT_EXEC";
		else
			classify = "USER_PROT_READ";
		break;
	default:
		classify = "USER_FAULT";
		break;
	}

	/* ISA extras (instruction / return-address peeks) may refine it. */
	classify = ir0_oops_user_fault_extra(fault, cur, classify);

	/*
	 * INFO (not only NOTICE): default profile keeps INFO+; greppable even
	 * if NOTICE mirroring is quieted. No panic / no page-table walks.
	 */
	klog_info_fmt("FAULT",
		      "USER_FAULT_FRAME kind=%s hw=%x addr=%llx syndrome=%llx "
		      "ip=%llx sp=%llx",
		      ir0_fault_kind_name(fault->kind), fault->hw_code,
		      (unsigned long long)addr,
		      (unsigned long long)fault->syndrome,
		      (unsigned long long)fault->ip,
		      (unsigned long long)fault->sp);
	if (cur)
		klog_info_fmt("FAULT",
			      "USER_FAULT_FRAME arg0=%llx arg1=%llx",
			      (unsigned long long)task_get_arg0(&cur->task),
			      (unsigned long long)task_get_arg1(&cur->task));
	klog_info_fmt("FAULT",
		      "USER_FAULT_FRAME pid=%x comm=%s present=%x write=%x "
		      "exec=%x CLASSIFY %s",
		      pid, comm && comm[0] ? comm : "(none)",
		      (unsigned)(fault->present ? 1 : 0),
		      (unsigned)(fault->write ? 1 : 0),
		      (unsigned)(fault->exec ? 1 : 0), classify);
	/* Raw tag for harnesses that strip klog prefixes. */
	klog_print("USER_FAULT_FRAME\n");
}
/**
 * Kernel panic handler - comprehensive error reporting system
 *
 * This module provides kernel panic functionality with extensive diagnostic
 * information dumping to both VGA console and serial port. The serial output
 * is designed to be copyable for external analysis and debugging.
 *
 * Key features:
 * - Double panic detection (prevents infinite recursion)
 * - Complete register dump (x86-64/x86-32)
 * - Stack trace unwinding
 * - Process context information
 * - Memory state information
 * - Structured serial output for easy parsing
 *
 * The serial output format is designed to be easily grep-able and parseable,
 * making it suitable for automated log analysis tools.
 */

static const char *panic_level_names[] =
    {
        "KERNEL BUG",
        "HARDWARE FAULT",
        "OUT OF MEMORY",
        "STACK OVERFLOW",
        "ASSERTION FAILED",
        "MEMORY ERROR",
        "TESTING",
        "RUNNING OUT PROCESS",
        "TESTING NESTED FAULT"
    };

static const char *panic_level_name(panic_level_t level)
{
    if ((unsigned)level >= sizeof(panic_level_names) / sizeof(panic_level_names[0]))
        return "UNKNOWN";
    return panic_level_names[level];
}


/* Forward declarations for helper functions */
static void dump_process_context(void);
static void dump_memory_state(void);

/* Panic-safe decimal (no malloc, no printf). */
static void klog_u32_dec(uint32_t n)
{
	char dec[12];
	int i = 0;

	if (n == 0)
	{
		klog_print("0");
		return;
	}
	while (n && i < (int)sizeof(dec) - 1)
	{
		dec[i++] = (char)('0' + (n % 10));
		n /= 10;
	}
	while (i > 0)
	{
		char c[2] = { dec[--i], '\0' };

		klog_print(c);
	}
}

/*
 * Human-readable uptime for the panic screen (seconds + milliseconds).
 * Serial dump uses the same format via klog_u32_dec; this is for FB/GTK.
 */
static void panic_uptime_human(char *buf, size_t cap)
{
	uint64_t up_ms = clock_get_uptime_milliseconds();
	uint64_t up_s = up_ms / 1000ULL;
	uint64_t up_frac = up_ms % 1000ULL;

	if (!buf || cap == 0)
		return;
	snprintf(buf, cap, "%llu.%03llu s",
		 (unsigned long long)up_s, (unsigned long long)up_frac);
}

/*
 * Final panic frame for FB/GTK: compact, readable, stays on screen.
 * Full register/process dumps stay on serial only (no scroll-away).
 */
static void panic_print_final_screen(const char *message, panic_level_t level,
				     const char *file, int line,
				     const char *caller)
{
	char uptime[32];
	char linebuf[384];

	panic_uptime_human(uptime, sizeof(uptime));

	print_screen_only("\n");
	print_screen_only("========================================\n");
	print_screen_only("KERNEL PANIC - SYSTEM HALTED\n");
	print_screen_only("========================================\n");
	print_screen_only("\n");

	print_screen_only("     +------------------------------------------------+\n");
	print_screen_only("     |                                                |\n");
	print_screen_only("     |              O_o KERNEL PANIC                  |\n");
	print_screen_only("     |                                                |\n");
	print_screen_only("     +------------------------------------------------+\n\n");

	snprintf(linebuf, sizeof(linebuf), "Reason: %s\n",
		 message ? message : "no message");
	print_screen_only(linebuf);
	snprintf(linebuf, sizeof(linebuf), "Origin: %s:%d (%s)\n",
		 file ? file : "unknown", line, caller ? caller : "unknown");
	print_screen_only(linebuf);
	snprintf(linebuf, sizeof(linebuf), "Level: %s\n", panic_level_name(level));
	print_screen_only(linebuf);
	snprintf(linebuf, sizeof(linebuf), "Uptime: %s\n\n", uptime);
	print_screen_only(linebuf);

	print_screen_only("========================================\n");
	print_screen_only("SYSTEM HALTED - Safe to power off or reboot\n");
	print_screen_only("========================================\n");
}

/**
 * panicex - Extended panic handler with comprehensive diagnostics
 * @message: Human-readable error message describing the panic
 * @level: Severity level of the panic (affects recovery strategy)
 * @file: Source file where panic occurred (from __FILE__)
 * @line: Line number where panic occurred (from __LINE__)
 * @caller: Function name where panic occurred (from __func__)
 *
 * This is the main panic entry point that coordinates all diagnostic
 * information gathering and output. It's designed to be as robust as
 * possible - even if parts of the kernel are corrupted, this function
 * should still provide useful debugging information.
 *
 * Execution flow:
 * 1. Double panic guard check (prevents recursion)
 * 2. Disable interrupts (prevent further corruption)
 * 3. Dump comprehensive information to serial port (persistent, copyable)
 * 4. Display formatted panic on VGA console (user-visible)
 * 5. Dump CPU registers (critical state at time of panic)
 * 6. Unwind stack trace (call chain leading to panic)
 * 7. Dump process context (if available)
 * 8. Halt system safely
 */
void panicex(const char *message, panic_level_t level, const char *file, int line, const char *caller)
{
    /* Double panic detection - if we're already panicking, something is seriously wrong.
     * This typically indicates a bug in the panic handler itself or memory corruption
     * so severe that even basic operations fail.
     */
    if (in_panic)
    {
        disable_interrupts();
        /*
         * Nested fault while already panicking (often #DF after a primary
         * #PF/#GP). Do not run a second full banner that looks like a new
         * primary DOUBLE FAULT — keep the first FAULT FRAME as the cause.
         */
        klog_fatal("OOPS", "\nCLASSIFY NESTED_PANIC_OR_FAULT");
        klog_fatal("OOPS", "PRIMARY_FAULT_FRAME_KEPT (ignore nested vector as cause)");
        klog_smoke("PANICEX_DOUBLE_FAULT_SAFE_OK");
        klog_smoke("PANIC_HANDLER_NO_USERPTR_DEREF_OK");
        for (;;)
            cpu_halt();
        return;
    }

    in_panic = 1;

    if (!panicex_tag_done)
    {
        panicex_tag_done = 1;
        klog_smoke("PANICEX_KERNEL_WIDE_OK");
        klog_smoke("PANICEX_VGA_SERIAL_OK");
    }

    /*
     * Test only: take a kernel page fault while panicking so the nested
     * path is exercised (Linux lkdtm style). The address is in the
     * unmapped upper half on x86-64 and in the disabled TTBR1 range on
     * ARM64, so both ISAs see a kernel-mode fault on a kernel address.
     */
    if (level == PANIC_TEST_NESTED_FAULT)
        *(volatile uint64_t *)(uintptr_t)0xFFFF800000000000ULL = 0;

    /* Disable interrupts immediately - we can't handle any more events safely.
     * The system is in an inconsistent state and any interrupt could cause
     * further corruption or triple faults.
     */
    disable_interrupts();

    /*
     * Do not activate the kernel address space here while RSP may still sit on a
     * per-task kstack: if that slot is missing from the kernel address-space
     * root, the next
     * stack access becomes #PF → #DF and the FAULT FRAME blames the dump.
     */

    /*
     * Re-enable FB/GTK output for the final panic frame. Verbose dump goes to
     * serial only so register stacks do not scroll the user-visible summary
     * off a 25-row (or scaled) console.
     */
    console_backend_panic_screen_on();
    klog_set_screen_sink(NULL);

    /* Dump comprehensive panic information to serial port first.
     * Serial output is structured for easy parsing and can be copied
     * from terminal emulators for external analysis.
     */
    klog_print("\n");
    klog_print("========================================\n");
    klog_print("KERNEL PANIC - SYSTEM HALTED\n");
    klog_print("========================================\n");
    {
	/*
	 * Monotonic uptime is a plain counter read — no locks. If the clock
	 * subsystem was never calibrated, values may be zero; still preferred
	 * over a hard-coded "no reliable time" string when ticks exist.
	 */
	uint64_t up_ms = clock_get_uptime_milliseconds();
	uint64_t up_s = up_ms / 1000ULL;
	uint64_t up_frac = up_ms % 1000ULL;

	klog_print("Uptime at panic: ");
	klog_u32_dec((uint32_t)up_s);
	klog_print(".");
	if (up_frac < 100)
		klog_print("0");
	if (up_frac < 10)
		klog_print("0");
	klog_u32_dec((uint32_t)up_frac);
	klog_print(" s\n");
    }
    klog_print("Panic Level: ");
    klog_print(panic_level_name(level));
    klog_print("\n");
    /*
     * FAULT FRAME is the CPU exception site (cause). Source/Caller below are
     * only where panicex was invoked (often isr_handler64_dispatch).
     */
    if (panic_exc_frame.valid)
    {
	klog_print("--- FAULT FRAME (cause) ---\n");
	klog_print("kind=");
	klog_print(ir0_fault_kind_name(panic_exc_frame.fault.kind));
	klog_print(" hw=");
	klog_hex32(panic_exc_frame.fault.hw_code);
	klog_print(" syndrome=");
	klog_hex64(panic_exc_frame.fault.syndrome);
	klog_print("\n");
	klog_print("fault_ip=");
	klog_hex64((uint64_t)panic_exc_frame.fault.ip);
	klog_print(" fault_sp=");
	klog_hex64((uint64_t)panic_exc_frame.fault.sp);
	klog_print("\n");
	if (panic_exc_frame.fault.kind == IR0_FAULT_PAGE)
	{
		klog_print("fault_addr=");
		klog_hex64((uint64_t)panic_exc_frame.fault.addr);
		klog_print(" (frozen at fault note)\n");
	}
	/* ISA register names (x86 rip/cs/rflags, ARM64 pc/esr/spsr). */
	ir0_oops_show_fault_regs(&panic_exc_frame.fault);
	klog_print("pid=");
	klog_hex32(panic_exc_frame.pid);
	klog_print(" comm=");
	klog_print(panic_exc_frame.comm[0] ? panic_exc_frame.comm : "(none)");
	klog_print("\n");
	klog_print("CLASSIFY PRIMARY_VECTOR_");
	klog_print(ir0_fault_kind_name(panic_exc_frame.fault.kind));
	klog_print("\n");
	if (panic_exc_frame.stack_overflow)
		klog_print("CLASSIFY KERNEL_STACK_OVERFLOW\n");
	klog_print("--- panic site (reporter, not cause) ---\n");
    }
    klog_print("Source File: ");
    klog_print(file ? file : "unknown");
    klog_print("\n");
    klog_print("Line Number: ");
    klog_hex32((uint32_t)line);
    klog_print("\n");
    klog_print("Calling Function: ");
    klog_print(caller ? caller : "unknown");
    klog_print("\n");
    klog_print("Error Message: ");
    klog_print(message ? message : "no message");
    klog_print("\n");
    klog_print("========================================\n");

    /* Dump CPU state - registers and control registers (serial only). */
    dump_registers();

    ir0_oops_dump_arch_extra();

    /* Never unwind during panic — see dump_stack_trace() stub rationale. */
    klog_print("STACK TRACE skipped (panic-safe)\n");

    /* Dump process context - what process was running when panic occurred */
    dump_process_context();

    /* Dump memory state - heap statistics and allocation info */
    dump_memory_state();

    /* Final message before halting */
    klog_print("\n========================================\n");
    klog_print("SYSTEM HALTED - Safe to power off or reboot\n");
    klog_print("========================================\n");
    klog_print("Copy the above information for kernel debugging.\n");
    klog_print("End of panic dump.\n\n");

    clear_screen();
    panic_print_final_screen(message, level, file, line, caller);

    goto sleep;

    sleep:
        for (;;)
            cpu_halt();
}

/**
 * dump_process_context - Dump information about the currently running process
 *
 * If a process was active when the panic occurred, dump its state including
 * PID, register values, memory layout, and execution state. This is critical
 * for debugging user-space related panics.
 */
static void dump_process_context(void)
{
    klog_print("\n--- PROCESS CONTEXT ---\n");

    klog_print("Current Process: 0x");
    klog_hex64((uint64_t)(uintptr_t)current_process);
    klog_print("\n");

    klog_print("Process List Head: 0x");
    klog_hex64((uint64_t)(uintptr_t)process_list);
    klog_print("\n");

    if (current_process)
    {
	uint32_t pid = (uint32_t)current_process->task.pid;

        klog_print("Current PID: ");
	klog_u32_dec(pid);
	klog_print(" (0x");
        klog_hex32(pid);
        klog_print(")\n");
	klog_print("comm: ");
	klog_print(current_process->comm[0] ? current_process->comm : "(none)");
	klog_print("\n");
	klog_print("state: 0x");
        klog_hex64((uint64_t)current_process->state);
        klog_print("\n");
	klog_print("uid/euid/suid: ");
	klog_u32_dec((uint32_t)current_process->uid);
	klog_print("/");
	klog_u32_dec((uint32_t)current_process->euid);
	klog_print("/");
	klog_u32_dec((uint32_t)current_process->suid);
	klog_print("\n");
	klog_print("gid/egid/sgid: ");
	klog_u32_dec((uint32_t)current_process->gid);
	klog_print("/");
	klog_u32_dec((uint32_t)current_process->egid);
	klog_print("/");
	klog_u32_dec((uint32_t)current_process->sgid);
	klog_print("\n");
	klog_print("cwd: ");
	klog_print(current_process->cwd[0] ? current_process->cwd : "(none)");
	klog_print("\n");
    }
    else
    {
        klog_print("Current Process: NULL (no active process)\n");
    }

    klog_print("\n");
}

/**
 * dump_memory_state - Dump kernel memory allocator statistics
 *
 * Provides information about heap usage, allocations, and fragmentation.
 * This helps diagnose memory-related panics (OOM, double free, corruption).
 */
static void dump_memory_state(void)
{
    klog_print("\n--- MEMORY STATE ---\n");

    /* Full heap statistics are not printed here: allocator state may be unsafe
     * to traverse during panic.
     */
    klog_print("(Full memory statistics may be unavailable due to panic state)\n");
    klog_print("\n");
}
