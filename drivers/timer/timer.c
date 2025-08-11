// En un archivo timer.c o en scheduler.c
#include "clock_system.h"
#include "../../kernel/scheduler/scheduler.h"
#include "lapic/lapic.h"    

static enum ClockType current_timer_type = CLOCK_RTC; // Se inicializa en init_clock()

void time_handler() 
{
    // Llamar al scheduler
    scheduler_tick();
    
    // Enviar EOI según el timer usado
    switch (current_timer_type) 
    {
        case CLOCK_HPET:
            // HPET maneja sus propias interrupciones
            break;
            
        case CLOCK_LAPIC:
            // LAPIC EOI
            lapic_send_eoi(); // Necesitas implementar esto
            break;
            
        case CLOCK_PIT:
            // PIT usa PIC, enviar EOI al PIC maestro
            outb(0x20, 0x20);
            break;
            
        case CLOCK_RTC:
            // RTC también usa PIC
            outb(0x20, 0x20);
            break;
    }
}



