// drivers/timer/clock_system.c - SIMPLIFICADO
#include <print.h>
#include "acpi/acpi.h"
#include "pit/pit.h"
// #include "lapic/lapic.h"  // DESHABILITADO TEMPORALMENTE
#include "hpet/hpet.h"
#include "clock_system.h"
#include <panic/panic.h>

static enum ClockType current_timer_type = CLOCK_NONE;

enum ClockType get_current_timer_type(void)
{
    return current_timer_type;
}

void init_clock()
{
    print_colored("=== Inicializando Sistema de Relojes ===\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);

    current_timer_type = detect_best_clock();

    switch (current_timer_type)
    {
    case CLOCK_HPET:
        LOG_OK("[CLOCK] Using HPET");
        hpet_init();
        break;

    // case CLOCK_LAPIC:  // DESHABILITADO TEMPORALMENTE
    //     LOG_OK("[CLOCK] Using LAPIC Timer");
    //     lapic_init_timer();
    //     break;

    case CLOCK_PIT:
        LOG_OK("[CLOCK] HPET/LAPIC unavailable, using PIT");
        init_PIT(100); // 100 Hz = 10ms per tick
        break;

    case CLOCK_RTC:
        LOG_OK("[CLOCK] Using legacy RTC timer");
        // rtc_timer_init();  // implementar
        break;

    default:
        LOG_ERR("[CLOCK] No suitable timer found!");
        panic("No timer available!");
    }

    print_success("Sistema de relojes inicializado\n");
}


