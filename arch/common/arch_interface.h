#pragma once 
#include <stdint.h>

//Permitir interrupciones "sti"
void  arch_enable_interrupts(void);

// Funcion OUTB para poder setear el registro de control. Tiene que ser portable para poder trabajar en mas de una arquitectura
static inline void outb(uint16_t port, uint8_t value);


// Por ahora, voy a incluir funciones que deba exportar para mantener la portabilidad y la escalabilidad hacia otras arquitecturas.
uintptr_t read_fault_address(); 



