#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
extern void idt_load32_asm(uint32_t idt_ptr_addr);

void idt_load32(void)
{
    idt_load32_asm((uint32_t)&idt_ptr);
}
