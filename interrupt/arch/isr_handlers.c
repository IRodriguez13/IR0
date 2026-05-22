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

static int is_user_exception_frame(uint64_t *stack)
{
    if (!stack)
        return 0;
    return ((stack[3] & 0x3U) == 0x3U);
}

/* Handler de interrupciones para 64-bit */
void isr_handler64(uint64_t interrupt_number, uint64_t *stack)
{
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

        /* FASE 23: identify which kernel exception is firing before panicking. */
        {
            extern void serial_print(const char *);
            extern void serial_print_hex64(uint64_t);
            extern uint64_t fase23_copy_region_probe[8];
            serial_print("FASE23_KERNEL_EXC vector=");
            serial_print_hex64(interrupt_number);
            if (stack)
            {
                serial_print(" stk[0]=");
                serial_print_hex64(stack[0]);
                serial_print(" stk[1]=");
                serial_print_hex64(stack[1]);
                serial_print(" stk[2]=");
                serial_print_hex64(stack[2]);
                serial_print(" stk[3]=");
                serial_print_hex64(stack[3]);
            }
            serial_print("\nFASE23_CP_REGION dst=");
            serial_print_hex64(fase23_copy_region_probe[0]);
            serial_print(" n=");
            serial_print_hex64(fase23_copy_region_probe[1]);
            serial_print(" dst+n=");
            serial_print_hex64(fase23_copy_region_probe[2]);
            serial_print(" pml4=");
            serial_print_hex64(fase23_copy_region_probe[3]);
            serial_print("\nFASE23_CP_REGION page=");
            serial_print_hex64(fase23_copy_region_probe[4]);
            serial_print(" pte_present=");
            serial_print_hex64(fase23_copy_region_probe[5]);
            serial_print(" phys=");
            serial_print_hex64(fase23_copy_region_probe[6]);
            serial_print(" seq=");
            serial_print_hex64(fase23_copy_region_probe[7]);
            serial_print("\n");
        }
        panic("Unhandled kernel CPU exception");
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
