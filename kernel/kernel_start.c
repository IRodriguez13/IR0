// kernel/kernel_start.c - ACTUALIZADO CON RUTAS CORRECTAS
#include "../drivers/timer/clock_system.h"
#include "scheduler/scheduler.h"
#include "scheduler/task.h"
#include "../arch/common/arch_interface.h"
#include <kernel.h>
#include "../memory/ondemand-paging.h"
// ARREGLADO: Includes con rutas correctas según arquitectura
#if defined(__i386__)
#include "../memory/arch/x_86-32/Paging_x86-32.h"
#define init_paging() init_paging_x86()
#elif defined(__x86_64__)
#include "../memory/arch/x_64/Paging_x64.h" // ME FALTA PAGINACION DE 64 BIT
#define init_paging() init_paging_x64()
#else
#error "Arquitectura no soportada en kernel_start.c"
#endif

void main()
{
    clear_screen();
    print_colored("=== IR0 KERNEL BOOT ===\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);

    // 1. IDT primero (sin logging avanzado aún)
    idt_init();

    // 2. Paginación básica
    init_paging();

    // 3. CRITICAL: Inicializar sistema de memoria ANTES que cualquier kmalloc()
    memory_init();

    // 4. Ahora sí podemos usar logging avanzado y kmalloc()
    LOG_OK("Memory system initialized");

    // 5. Paginación on-demand (usa kmalloc internamente)
    ondemand_paging_init();
    LOG_OK("On-demand paging ready");

    // 6. Resto del sistema...
    scheduler_init();
    init_clock();
    arch_enable_interrupts();

    // Opcional: Crear algunas tareas de prueba
    create_test_tasks();

    print_colored("=== SYSTEM READY ===\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);

    // No usar scheduler_start() hasta tener procesos reales
    cpu_relax(); // Por ahora
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