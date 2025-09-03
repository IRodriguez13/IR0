#pragma once
#include <stdint.h>

void tss_init_x64(void);
void tss_load_x64(void);
void setup_tss();
uint64_t tss_get_address(void);
uint32_t tss_get_size(void);
