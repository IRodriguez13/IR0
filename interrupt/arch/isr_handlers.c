/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: isr_handlers.c
 * Description: Interrupt Service Routine handlers for exceptions, hardware interrupts, and system calls
 */

#include "idt.h"
#include "pic.h"
#include "io.h"
#include <ir0/vga.h>
#include <ir0/signals.h>
#include <ir0/oops.h>
#include <ir0/serial_io.h>
#include <ir0/debug_trap.h>
#include <kernel/process.h>
#include <config.h>
#include <ir0/input_backend.h>
#if CONFIG_ENABLE_NETWORKING
#include <ir0/net.h>
#endif

/* Declaraciones externas para el nuevo driver de teclado */
extern void keyboard_handler64(void);
extern void increment_pit_ticks(void);
#ifdef __x86_64__

extern void page_fault_handler_x64(uint64_t *stack);
extern void gpf_audit_from_isr(uint64_t *stack);
extern uint64_t get_current_page_directory(void);
extern uint64_t iretq_checkpoint_buf[40];
extern uint64_t isr_abi_entry_intno;
extern uint64_t isr_abi_entry_has_err;
extern uint64_t isr_abi_entry_rsp;
extern uint64_t isr_abi_entry_qwords[16];

static int is_exception_with_hw_error_code(uint64_t int_no)
{
    switch (int_no)
    {
        case 8:
        case 10:
        case 11:
        case 12:
        case 13:
        case 14:
        case 17:
            return 1;
        default:
            return 0;
    }
}

static void dump_qwords16(const char *tag, const uint64_t *q)
{
    int i;

    if (!q)
        return;

    serial_print(tag ? tag : "[ISRABI]");
    serial_print(" q0=");
    for (i = 0; i < 16; i++)
    {
        serial_print_hex64(q[i]);
        if (i != 15)
            serial_print(",");
    }
    serial_print("\n");
}

#if CONFIG_DEBUG_ISRABI
static void isr_abi_audit(uint64_t interrupt_number, uint64_t *stack)
{
    uint64_t c_qwords[16] = {0};
    uint64_t c_rip;
    uint64_t c_cs;
    uint64_t c_rflags;
    uint64_t c_rsp;
    uint64_t c_ss;
    uint64_t old_rip;
    uint64_t old_cs;
    uint64_t old_rsp;
    uint64_t old_ss;
    int hw_err_expected;
    int i;

    if (!stack)
        return;

    for (i = 0; i < 16; i++)
        c_qwords[i] = stack[i];

    hw_err_expected = is_exception_with_hw_error_code(interrupt_number);
    c_rip = stack[2];
    c_cs = stack[3];
    c_rflags = stack[4];
    c_rsp = stack[5];
    c_ss = stack[6];
    old_rip = stack[0];
    old_cs = stack[3];
    old_rsp = stack[5];
    old_ss = stack[6];

    serial_print("[ISRABI] int=");
    serial_print_hex64(interrupt_number);
    serial_print(" asm_int=");
    serial_print_hex64(isr_abi_entry_intno);
    serial_print(" asm_has_err=");
    serial_print_hex64(isr_abi_entry_has_err);
    serial_print(" hw_err_expected=");
    serial_print_hex64((uint64_t)hw_err_expected);
    serial_print(" asm_rsp=");
    serial_print_hex64(isr_abi_entry_rsp);
    serial_print(" c_stack=");
    serial_print_hex64((uint64_t)(uintptr_t)stack);
    serial_print("\n");

    dump_qwords16("[ISRABI][ASM_ENTRY]", isr_abi_entry_qwords);
    dump_qwords16("[ISRABI][C_FRAME]", c_qwords);

    serial_print("[ISRABI][LAYOUT] int=");
    serial_print_hex64(interrupt_number);
    serial_print(" real_rip=stack[2]=");
    serial_print_hex64(c_rip);
    serial_print(" real_cs=stack[3]=");
    serial_print_hex64(c_cs);
    serial_print(" real_rflags=stack[4]=");
    serial_print_hex64(c_rflags);
    serial_print(" real_rsp=stack[5]=");
    serial_print_hex64(c_rsp);
    serial_print(" real_ss=stack[6]=");
    serial_print_hex64(c_ss);
    serial_print("\n");

    serial_print("[ISRABI][LEGACY_MAP] int=");
    serial_print_hex64(interrupt_number);
    serial_print(" stack[0]=");
    serial_print_hex64(old_rip);
    serial_print(" stack[3]=");
    serial_print_hex64(old_cs);
    serial_print(" stack[5]=");
    serial_print_hex64(old_rsp);
    serial_print(" stack[6]=");
    serial_print_hex64(old_ss);
    serial_print("\n");
}
#endif

static void isr_contract_single_check(uint64_t interrupt_number, uint64_t *stack)
{
    uint64_t marker_b;
    uint64_t expected_rip;
    uint64_t expected_cs;
    uint64_t expected_rflags;
    uint64_t expected_rsp;
    uint64_t expected_ss;
    uint64_t actual_fault_rip;
    uint64_t actual_fault_cs;
    uint64_t actual_fault_rsp;
    const char *classification = "CONTEXT_RESTORE_BROKEN";

    if (!stack || interrupt_number >= 32)
        return;

    if (ir0_panic_in_progress())
        return;

    marker_b = iretq_checkpoint_buf[8];
    expected_rip = iretq_checkpoint_buf[9];
    expected_cs = iretq_checkpoint_buf[10];
    expected_rflags = iretq_checkpoint_buf[11];
    expected_rsp = iretq_checkpoint_buf[12];
    expected_ss = iretq_checkpoint_buf[13];

    actual_fault_rip = stack[2];
    actual_fault_cs = stack[3];
    actual_fault_rsp = stack[5];

    if (marker_b != 0xBBB1ULL)
        classification = "CONTEXT_LIFETIME_BROKEN";
    else if ((expected_cs & 0x3ULL) != 0x3ULL && (actual_fault_cs & 0x3ULL) == 0x0ULL)
        classification = "CONTEXT_RESTORE_BROKEN";
    else if ((expected_cs & 0x3ULL) == 0x3ULL && (actual_fault_cs & 0x3ULL) == 0x0ULL)
        classification = "IRET_TRANSITION_BROKEN";
    else if (expected_cs != actual_fault_cs && (actual_fault_cs & 0x3ULL) == 0x3ULL)
        classification = "CONTEXT_LAYOUT_MISMATCH";
    else if (expected_rip != actual_fault_rip || expected_rsp != actual_fault_rsp)
        classification = "CONTEXT_COPY_BROKEN";

    serial_print("[CONTRACT][PRE_IRET_RAW] marker=");
    serial_print_hex64(marker_b);
    serial_print(" rip=");
    serial_print_hex64(expected_rip);
    serial_print(" cs=");
    serial_print_hex64(expected_cs);
    serial_print(" rflags=");
    serial_print_hex64(expected_rflags);
    serial_print(" rsp=");
    serial_print_hex64(expected_rsp);
    serial_print(" ss=");
    serial_print_hex64(expected_ss);
    serial_print("\n");

    serial_print("[CONTRACT][POST_FAULT_RAW] int=");
    serial_print_hex64(interrupt_number);
    serial_print(" rip=");
    serial_print_hex64(actual_fault_rip);
    serial_print(" cs=");
    serial_print_hex64(actual_fault_cs);
    serial_print(" rsp=");
    serial_print_hex64(actual_fault_rsp);
    serial_print("\n");

    serial_print("[CONTRACT][COMPARE] expected_rip=");
    serial_print_hex64(expected_rip);
    serial_print(" actual_fault_rip=");
    serial_print_hex64(actual_fault_rip);
    serial_print(" expected_cs=");
    serial_print_hex64(expected_cs);
    serial_print(" actual_fault_cs=");
    serial_print_hex64(actual_fault_cs);
    serial_print(" expected_rsp=");
    serial_print_hex64(expected_rsp);
    serial_print(" actual_fault_rsp=");
    serial_print_hex64(actual_fault_rsp);
    serial_print("\n");

    serial_print("[CONTRACT][CLASSIFY] ");
    serial_print(classification);
    serial_print("\n");
}

static int is_user_exception_frame(uint64_t *stack)
{
    if (!stack)
        return 0;
    return ((stack[3] & 0x3U) == 0x3U);
}

/* Handler de interrupciones para 64-bit */
void isr_handler64(uint64_t interrupt_number, uint64_t *stack)
{
#if defined(__x86_64__) || defined(__amd64__)
    if (interrupt_number == 1 && stack)
    {
        if (is_user_exception_frame(stack))
        {
            if (ir0_debug_handle_user_db(stack))
                return;
        }
        else
        {
            ir0_debug_handle_kernel_db(stack);
            return;
        }
    }
#endif

    if (interrupt_number < 32 && stack)
        isr_contract_single_check(interrupt_number, stack);

    if (interrupt_number < 32 && stack)
#if CONFIG_DEBUG_ISRABI
        isr_abi_audit(interrupt_number, stack);
#endif

    if (interrupt_number < 32 && stack)
    {
        process_t *current = process_get_current();
        uint64_t user = (uint64_t)is_user_exception_frame(stack);

        serial_print("[ISR] int=");
        serial_print_hex64(interrupt_number);
        serial_print(" current=");
        serial_print_hex64((uint64_t)(uintptr_t)current);
        serial_print(" cr3=");
        serial_print_hex64(get_current_page_directory());
        serial_print(" rip=");
        serial_print_hex64(stack[2]);
        serial_print(" cs=");
        serial_print_hex64(stack[3]);
        serial_print(" rsp=");
        serial_print_hex64(stack[5]);
        serial_print(" ss=");
        serial_print_hex64(stack[6]);
        serial_print(" frame=");
        serial_print_hex64((uint64_t)(uintptr_t)stack);
        serial_print(" user=");
        serial_print_hex64(user);
        serial_print("\n");
    }

    /* Manejar excepciones del CPU (0-31) */
    if (interrupt_number < 32)
    {
        process_t *current = process_get_current();
        int signal_to_send = 0;

        if (interrupt_number == 14)
        {
            page_fault_handler_x64(stack);
            return;
        }

        if (interrupt_number == 13 && stack)
        {
            gpf_audit_from_isr(stack);
        }

        /* Map CPU exceptions to signals for user-space faults */
        switch (interrupt_number)
        {
            case 0:  /* Divide by Zero */
                signal_to_send = SIGFPE;
                break;
            case 4:  /* Overflow */
                signal_to_send = SIGFPE;
                break;
            case 6:  /* Invalid Opcode */
                signal_to_send = SIGILL;
                break;
            case 8:  /* Double Fault - severe error */
                signal_to_send = SIGSEGV;
                break;
            case 11: /* Segment Not Present */
                signal_to_send = SIGSEGV;
                break;
            case 13: /* General Protection Fault */
                signal_to_send = SIGSEGV;
                break;
            case 14: /* Page Fault */
                signal_to_send = SIGSEGV;
                break;
            case 19: /* SIMD FPU Exception */
                signal_to_send = SIGFPE;
                break;
            default:
                signal_to_send = SIGSEGV;
                break;
        }

        /*
         * User-mode exceptions are converted to signals.
         * Kernel exceptions are fatal to avoid endless fault loops.
         */
        if (is_user_exception_frame(stack) && current && signal_to_send != 0)
        {
#if DEBUG_PROCESS
            print("Excepción CPU #");
            print_int32(interrupt_number);
            print(" -> SIG");
            print_int32(signal_to_send);
            print("\n");
#endif
            send_signal(current->task.pid, signal_to_send);
            return;
        }

        print("[ISR64] int=");
        print_hex(interrupt_number);

        print(" current=");
        print_hex((uint64_t)current);

        print(" user=");
        print_hex(is_user_exception_frame(stack));

        print(" cs=");
        print_hex(stack[3]);

        print(" rip=");
        print_hex(stack[0]);

        print("\n");

        panicex("Unhandled kernel CPU exception", PANIC_KERNEL_BUG, __FILE__, __LINE__, __func__);
    }

    /* Manejar syscall (0x80) */
    if (interrupt_number == 128)
    {
        return;
    }

    /* Manejar IRQs del PIC (32-47) */
    if (interrupt_number >= 32 && interrupt_number <= 47)
    {
        uint8_t irq = interrupt_number - 32;

        switch (irq)
        {
        case 0: /* Timer */
        {
            irq_save_user_frame(stack);
            increment_pit_ticks();
            break;
        }

        case 1: /* Keyboard */
            keyboard_handler64();
            break;

        case 12: /* PS/2 Mouse */
        {
            input_mouse_handle_interrupt();
            break;
        }

        default:
            break;
        }

#if CONFIG_ENABLE_NETWORKING
        net_stack_handle_irq(irq);
#endif

        /* Enviar EOI para IRQs */
        pic_send_eoi64(irq);
        return;
    }

}

#endif
