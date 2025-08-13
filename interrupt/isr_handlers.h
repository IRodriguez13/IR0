#pragma  once
#include <print.h>
#include <stdint.h>
#include <panic/panic.h>
#include "../drivers/timer/lapic/lapic.h"   
#include "../kernel/scheduler/scheduler.h" 

void default_interrupt_handler();
void page_fault_handler();
void time_handler(); 