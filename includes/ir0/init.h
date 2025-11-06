/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef _IR0_INIT_H
#define _IR0_INIT_H

#include <stdint.h>

/* Architecture initialization */
extern void gdt_install(void);
extern void setup_tss(void);
extern void idt_init64(void);
extern void idt_load64(void);

/* Interrupt controller */
extern void pic_remap64(void);
extern void pic_unmask_irq(uint8_t irq);

/* Subsystem initialization */
extern void logging_init(void);
extern void serial_init(void);
extern void ps2_init(void);
extern void keyboard_init(void);
extern void simple_alloc_init(void);
extern void ata_init(void);
extern void process_init(void);
extern void syscalls_init(void);

/* Complex subsystems */
extern int vfs_init_with_minix(void);
extern int clock_system_init(void);
extern int scheduler_cascade_init(void);
extern int start_init_process(void);

#endif /* _IR0_INIT_H */