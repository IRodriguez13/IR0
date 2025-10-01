#pragma once 
#include <stdint.h>

void kmain_x32(void); 

void arch_enable_interrupts(void); 

const char* arch_get_name(void); 

uintptr_t read_fault_address(); 

void outb(uint16_t port, uint8_t value);



