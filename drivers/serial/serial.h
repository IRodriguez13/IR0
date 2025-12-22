#pragma once

#include <stdint.h>

/* Serial port definitions */
#define SERIAL_PORT_COM1        0x3F8

/* UART Registers (offsets from base) */
#define SERIAL_DATA_REG         0
#define SERIAL_INT_EN_REG       1
#define SERIAL_FIFO_CTRL_REG    2
#define SERIAL_LINE_CTRL_REG    3
#define SERIAL_MODEM_CTRL_REG   4
#define SERIAL_LINE_STATUS_REG  5

/* Line Control Register bits */
#define SERIAL_LCR_8N1          0x03    /* 8 bits, no parity, 1 stop bit */
#define SERIAL_LCR_DLAB         0x80    /* Divisor Latch Access Bit */

/* Interrupt Enable Register bits */
#define SERIAL_IER_DISABLE      0x00    /* Disable all interrupts */

/* FIFO Control Register bits */
#define SERIAL_FCR_ENABLE       0x01    /* Enable FIFO */
#define SERIAL_FCR_CLR_RECV     0x02    /* Clear receiver FIFO */
#define SERIAL_FCR_CLR_XMIT     0x04    /* Clear transmitter FIFO */
#define SERIAL_FCR_TRIG_14      0xC0    /* Interrupt trigger level 14 bytes */
#define SERIAL_FCR_CONFIG       (SERIAL_FCR_ENABLE | SERIAL_FCR_CLR_RECV | SERIAL_FCR_CLR_XMIT | SERIAL_FCR_TRIG_14)

/* Modem Control Register bits */
#define SERIAL_MCR_DTR          0x01    /* Data Terminal Ready */
#define SERIAL_MCR_RTS          0x02    /* Request To Send */
#define SERIAL_MCR_OUT2         0x08    /* Auxiliary Output 2 */
#define SERIAL_MCR_CONFIG       (SERIAL_MCR_DTR | SERIAL_MCR_RTS | SERIAL_MCR_OUT2)

/* Line Status Register bits */
#define SERIAL_LSR_THRE         0x20    /* Transmitter holding register empty */

/* Baud rate divisors */
#define SERIAL_BAUD_38400       0x03    /* 115200 / 38400 = 3 */

/* ========================================================================== */
/* PUBLIC API                                                                 */
/* ========================================================================== */

void serial_init(void);
void serial_putchar(char c);
void serial_print(const char *str);
void serial_print_hex32(uint32_t num);
void serial_print_hex64(uint64_t num);
char serial_read_char(void);
