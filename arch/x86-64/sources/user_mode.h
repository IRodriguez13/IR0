#pragma once
#include <stdint.h>

void switch_to_user_mode(void *entry_point);
void syscall_handler_c(void);
