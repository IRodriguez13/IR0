/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: early_idt.c
 * Description: Minimal early IDT after GDT/TSS — COM1 dump + halt on fault.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <interrupt/arch/idt.h>
#include <stdint.h>

#define EARLY_COM1 0x3F8
#define IDT_TYPE_INTERRUPT_GATE 0x8E

extern void early_isr0(void);
extern void early_isr1(void);
extern void early_isr2(void);
extern void early_isr3(void);
extern void early_isr4(void);
extern void early_isr5(void);
extern void early_isr6(void);
extern void early_isr7(void);
extern void early_isr8(void);
extern void early_isr9(void);
extern void early_isr10(void);
extern void early_isr11(void);
extern void early_isr12(void);
extern void early_isr13(void);
extern void early_isr14(void);
extern void early_isr15(void);
extern void early_isr16(void);
extern void early_isr17(void);
extern void early_isr18(void);
extern void early_isr19(void);
extern void early_isr20(void);
extern void early_isr21(void);
extern void early_isr22(void);
extern void early_isr23(void);
extern void early_isr24(void);
extern void early_isr25(void);
extern void early_isr26(void);
extern void early_isr27(void);
extern void early_isr28(void);
extern void early_isr29(void);
extern void early_isr30(void);
extern void early_isr31(void);

static void early_com1_putc(char c)
{
	volatile uint16_t port = EARLY_COM1;
	int spins = 100000;

	while (spins-- > 0)
	{
		uint8_t lsr;

		__asm__ volatile("inb %1, %0" : "=a"(lsr) : "Nd"((uint16_t)(port + 5)));
		if (lsr & 0x20)
			break;
	}
	__asm__ volatile("outb %0, %1" : : "a"(c), "Nd"(port));
}

static void early_com1_puts(const char *s)
{
	if (!s)
		return;
	while (*s)
		early_com1_putc(*s++);
}

static void early_com1_hex64(uint64_t v)
{
	static const char hex[] = "0123456789abcdef";
	int i;

	early_com1_puts("0x");
	for (i = 60; i >= 0; i -= 4)
		early_com1_putc(hex[(v >> i) & 0xf]);
}

void early_exception_halt(uint64_t vec, uint64_t err, uint64_t rip, uint64_t cr2)
{
	early_com1_puts("\r\n[EARLY_IDT] exception vec=");
	early_com1_hex64(vec);
	early_com1_puts(" err=");
	early_com1_hex64(err);
	early_com1_puts(" rip=");
	early_com1_hex64(rip);
	early_com1_puts(" cr2=");
	early_com1_hex64(cr2);
	early_com1_puts("\r\n");
	for (;;)
		__asm__ volatile("cli; hlt");
}

void idt_early_install64(void)
{
	static void (*const stubs[32])(void) = {
		early_isr0,  early_isr1,  early_isr2,  early_isr3,
		early_isr4,  early_isr5,  early_isr6,  early_isr7,
		early_isr8,  early_isr9,  early_isr10, early_isr11,
		early_isr12, early_isr13, early_isr14, early_isr15,
		early_isr16, early_isr17, early_isr18, early_isr19,
		early_isr20, early_isr21, early_isr22, early_isr23,
		early_isr24, early_isr25, early_isr26, early_isr27,
		early_isr28, early_isr29, early_isr30, early_isr31,
	};
	int i;

	idt_ptr.limit = (sizeof(struct idt_entry64) * 256) - 1;
	idt_ptr.base = (uint64_t)&idt;

	for (i = 0; i < 256; i++)
		idt_set_gate64((uint8_t)i, (uint64_t)stubs[0], 0x08,
			       IDT_TYPE_INTERRUPT_GATE, 0);

	for (i = 0; i < 32; i++)
	{
		uint8_t ist = (i == 8) ? 1 : 0;

		idt_set_gate64((uint8_t)i, (uint64_t)stubs[i], 0x08,
			       IDT_TYPE_INTERRUPT_GATE, ist);
	}

	idt_load64();
}
