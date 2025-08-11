#pragma once
#include <stdint.h>

void lapic_init_timer();
int lapic_available();
void lapic_send_eoi();
