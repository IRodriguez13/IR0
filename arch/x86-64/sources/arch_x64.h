#pragma once
#include <stdint.h>

void outb(uint16_t port, uint8_t value);
uint8_t inb(uint16_t port);

const char* arch_get_name(void);

void arch_enable_interrupts(void);

void kmain_x32(void);

uintptr_t read_fault_address();
