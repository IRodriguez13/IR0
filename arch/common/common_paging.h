#pragma once 
#include <stdint.h>

#if defined(__x86_64__)
    #include "../x_64/sources/idt_arch_x64.h"
#elif defined(__i386__) // 32 bit por si no la cazaste
    #include "../x86-32/sources/idt_arch_x86.h"
#else
    #error "Arquitectura no soportada"
#endif



// Esta función le dice a la CPU que empiece a paginar configurando el directorio. -- 32 bit --
void paging_set_cpu();