/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef _IR0_SERIAL_H
#define _IR0_SERIAL_H

#include <stdint.h>

/* Serial port functions */
extern void serial_print(const char *str);
extern void serial_print_hex32(uint32_t num);

#endif /* _IR0_SERIAL_H */