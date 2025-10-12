#pragma once
#include <stdint.h>

void jmp_ring3(void *entry_point);
void syscall_handler_c(void);
