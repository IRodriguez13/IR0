// kernel/kernel_start.c - ACTUALIZADO CON RUTAS CORRECTAS
#include "../drivers/timer/clock_system.h"
#include "scheduler/scheduler.h"
#include "scheduler/task.h"
#include "../arch/common/arch_interface.h"
#include <kernel.h>
#include "../memory/ondemand-paging.h"
#include "../memory/physical_allocator.h"
#include "../memory/heap_allocator.h"
#include "../memory/memo_interface.h"
#include "../arch/common/idt.h"
// ARREGLADO: Includes con rutas correctas según arquitectura
#if defined(__i386__)
#include "../memory/arch/x_86-32/Paging_x86-32.h"
#define init_paging() init_paging_x86()
#elif defined(__x86_64__)
#include "../memory/arch/x86-64/Paging_x64.h"
#define init_paging() init_paging_x64()
#else
#error "Arquitectura no soportada en kernel_start.c"
#endif

void main()
{
    clear_screen();
    print_colored("=== IR0 KERNEL BOOT ===\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);

    // 1. IDT primero (ANTES que cualquier cosa que pueda generar interrupts)
    idt_init();
    LOG_OK("IDT initialized");

    // 2. Physical allocator (NO necesita kmalloc)
    physical_allocator_init();
    LOG_OK("Physical allocator ready");

    // 3. Paginación básica (NO necesita kmalloc)
    init_paging();
    LOG_OK("Basic paging enabled");

    // 4. AHORA SÍ heap allocator (puede usar physical_allocator)
    heap_allocator_init();
    LOG_OK("Kernel heap ready");

    // 5. Memory system wrapper (conecta todo)
    memory_init();
    LOG_OK("Memory system unified");

    // 6. On-demand paging (AHORA puede usar kmalloc)
    ondemand_paging_init();
    LOG_OK("On-demand paging active");

    // 7. Scheduler (puede usar kmalloc para structures)
    scheduler_init();

    // 8. Clock system
    init_clock();

    // 9. Habilitar interrupts AL FINAL
    arch_enable_interrupts();

    print_colored("=== SYSTEM READY ===\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);

    // Crear tareas de prueba
    create_test_tasks();

    // NO usar scheduler_start() hasta tener procesos reales
    scheduler_dispatch_loop();
}


void ShutDown()
{
    print_warning("System shutdown requested\n");

    // Intentar apagar via ACPI primero
    outb(0x604, 0x00); // QEMU/ACPI shutdown

    uint8_t reset_value = 0xFE;
    asm volatile(
        "outb %%al, $0x64"
        :
        : "a"(reset_value)
        : "memory");

    // Último recurso: hang
    print_error("Shutdown failed, hanging CPU\n");
    cpu_relax();
}