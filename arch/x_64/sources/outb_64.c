#include <stdint.h>


static inline void outb(uint16_t port, uint8_t value)
{
    asm volatile("outb %0, %1"::"a"(value),"Nd"(port));
} 