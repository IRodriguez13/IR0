#pragma once
#include <stdint.h>

void init_PIT(uint32_t frequency);
void init_pic(void);
uint32_t get_pit_ticks(void);
void increment_pit_ticks(void);

