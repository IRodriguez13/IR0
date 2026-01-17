/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - x86-64 Early Architecture Initialization
 * Copyright (C) 2025 Iván Rodriguez
 *
 * This file implements the x86-64 specific early initialization
 * that is called through the portable architecture interface.
 */

#include <arch/common/arch_portable.h>
#include <arch/x86-64/sources/gdt.h>
#include <arch/x86-64/sources/tss_x64.h>
#include <interrupt/arch/idt.h>
#include <interrupt/arch/pic.h>
#include <drivers/serial/serial.h>

/**
 * arch_early_init_x86_64 - x86-64 early architecture initialization
 *
 * Sets up GDT (Global Descriptor Table) and TSS (Task State Segment)
 * required for x86-64 operation. Called before any other subsystems.
 * This function is called via arch_early_init() wrapper.
 */
void arch_early_init_x86_64(void)
{
    /* Initialize GDT (Global Descriptor Table) */
    gdt_install();
    
    /* Initialize TSS (Task State Segment) for user mode support */
    setup_tss();
    
    serial_print("[ARCH] x86-64 early init: GDT and TSS initialized\n");
}

/**
 * arch_interrupt_init_x86_64 - x86-64 interrupt system initialization
 *
 * Sets up IDT (Interrupt Descriptor Table) and PIC (Programmable Interrupt Controller).
 * Called after core subsystems but before enabling interrupts.
 * This function is called via arch_interrupt_init() wrapper.
 */
void arch_interrupt_init_x86_64(void)
{
    /* Initialize IDT (Interrupt Descriptor Table) */
    idt_init64();
    idt_load64();
    
    /* Remap PIC (Programmable Interrupt Controller) to IRQs 32-47 */
    pic_remap64();
    
    serial_print("[ARCH] x86-64 interrupt init: IDT and PIC initialized\n");
}

