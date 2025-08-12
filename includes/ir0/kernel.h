#pragma once

#include <ir0/print.h>
#include "../../arch/common/idt.h"
#include <panic/panic.h>

// Prototipos de funciones
void init_paging_x86(void);
void init_paging_x64(void);
void dump_scheduler_state(void);
void ShutDown(void);