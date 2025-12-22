#pragma once
#include <stdint.h>

/* PIC (Programmable Interrupt Controller) Registers */
#define PIC1_COMMAND    0x20
#define PIC1_DATA       0x21
#define PIC2_COMMAND    0xA0
#define PIC2_DATA       0xA1

/* PIC Commands */
#define PIC_ICW1_INIT   0x11 /* ICW1: initialization */
#define PIC_ICW4_8086   0x01 /* ICW4: 8086/88 (MCS-80/85) mode */

/* PIT (Programmable Interval Timer) Registers */
#define PIT_REG_CHAN0   0x40
#define PIT_REG_COMMAND 0x43

/* PIT Commands */
#define PIT_CMD_CHAN0   0x00
#define PIT_CMD_LOHI    0x30
#define PIT_CMD_MODE3   0x06
#define PIT_COMMAND_VAL (PIT_CMD_CHAN0 | PIT_CMD_LOHI | PIT_CMD_MODE3)

#define PIT_BASE_FREC   1193180

void init_PIT(uint32_t frequency);
void init_pic(void);
uint32_t get_pit_ticks(void);
void increment_pit_ticks(void);

