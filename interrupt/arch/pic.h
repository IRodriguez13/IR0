#ifndef PIC_H
#define PIC_H

#include <stdint.h>

/* I/O ports */
#define PIC1_COMMAND    0x20
#define PIC1_DATA       0x21
#define PIC2_COMMAND    0xA0
#define PIC2_DATA       0xA1

/* ICW1 */
#define ICW1_ICW4       0x01 /* ICW4 needed */
#define PIC_ICW1_INIT   (ICW1_INIT | ICW1_ICW4) /* Start init sequence; ICW4 follows */
#define ICW1_SINGLE     0x02 /* Single (cascade) mode */
#define ICW1_INTERVAL4  0x04 /* Call address interval 4 (8) */
#define ICW1_LEVEL      0x08 /* Level triggered (edge) mode */
#define ICW1_INIT       0x10 /* Initialization - required */

/* ICW4 */
#define ICW4_8086       0x01 /* 8086/88 (MCS-80/85) mode */
#define ICW4_AUTO       0x02 /* Auto (normal) EOI */
#define ICW4_BUF_SLAVE  0x08 /* Buffered mode/slave */
#define ICW4_BUF_MASTER 0x0C /* Buffered mode/master */
#define ICW4_SFNM       0x10 /* Special fully nested (not) */

/* ICW3: master tells which IRQ line connects the slave; slave cascade ID */
#define PIC_ICW3_MASTER 0x04 /* Slave wired to master's IRQ2 */
#define PIC_ICW3_SLAVE 0x02 /* Slave identity on cascade (IRQ2) */

#define PIC_ICW4_8086 ICW4_8086

/* EOI (End of Interrupt) */
#define PIC_EOI         0x20

/* Vector bases after remap (master IRQ0-7 -> 0x20-0x27, slave -> 0x28-0x2F) */
#define IRQ_BASE_MASTER 0x20
#define IRQ_BASE_SLAVE  0x28

/* IRQ line numbers */
#define IRQ_TIMER       0
#define IRQ_KEYBOARD    1
#define IRQ_CASCADE     2
#define IRQ_COM2        3
#define IRQ_COM1        4
#define IRQ_LPT2        5
#define IRQ_FLOPPY      6
#define IRQ_LPT1        7
#define IRQ_CMOS        8
#define IRQ_FREE1       9
#define IRQ_FREE2       10
#define IRQ_FREE3       11
#define IRQ_PS2         12
#define IRQ_FPU         13
#define IRQ_ATA1        14
#define IRQ_ATA2        15

void pic_remap64(void);
void pic_send_eoi64(uint8_t irq);
void pic_mask_irq(uint8_t irq);
void pic_unmask_irq(uint8_t irq);

#endif /* PIC_H */
