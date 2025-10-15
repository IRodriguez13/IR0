#pragma once

void serial_init(void);
void serial_putchar(char c);
void serial_print(const char *str);
void serial_print_hex32(uint32_t num);
void serial_print_hex64(uint64_t num);
