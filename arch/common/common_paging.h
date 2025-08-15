#pragma once 
#include <stdint.h>

// Declaraciones comunes de paginación
// Las implementaciones específicas están en los archivos de cada arquitectura

// Esta función le dice a la CPU que empiece a paginar configurando el directorio. -- 32 bit --
void paging_set_cpu(uint32_t page_directory);

// Esta función le dice a la CPU que empiece a paginar configurando el directorio. -- 64 bit --
void paging_set_cpu_x64(uint64_t page_directory);
