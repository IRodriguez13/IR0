#include <stdint.h>

uintptr_t read_fault_address() 
{
    uintptr_t addr;
    asm volatile("mov %%cr2, %0" : "=r"(addr));
    return addr;
}
