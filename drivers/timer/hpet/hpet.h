#ifndef HPET_H
#define HPET_H

#include <stdint.h>
#include <stddef.h>

void hpet_init();
void hpet_set_address(void* addr);

#endif
