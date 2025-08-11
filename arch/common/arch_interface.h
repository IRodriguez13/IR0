#pragma once 
#include <stdint.h>

void  arch_enable_interrupts(void);


// Por ahora, voy a incluir funciones que deba exportar para mantener la portabilidad y la escalabilidad hacia otras arquitecturas.
uintptr_t read_fault_address(); 
