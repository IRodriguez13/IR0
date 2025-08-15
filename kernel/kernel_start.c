// kernel/kernel_start.c - VERSIÓN ESTABLE
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
#include "../fs/vfs_simple.h"
#include "../arch/common/arch_interface.h" // Para inb/outb

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

// Función para delay simple
static void delay_ms(uint32_t ms)
{
    // Delay simple usando loops
    for (volatile uint32_t i = 0; i < ms * 100000; i++)
    {
        __asm__ volatile("nop");
    }
}

void main()
{
    clear_screen();
    print_colored("=== IR0 KERNEL BOOT ===\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    delay_ms(1000);

    print("main: Iniciando inicialización del kernel...\n");
    delay_ms(500);

    // 1. IDT primero (ANTES que cualquier cosa que pueda generar interrupts)
    print("main: Inicializando IDT...\n");
    delay_ms(200);
    idt_init();
    LOG_OK("IDT initialized");
    print("main: IDT inicializado correctamente\n");
    delay_ms(500);

    // 2. Timer system (ANTES del scheduler) - SIN habilitar interrupciones
    print("main: Inicializando timer (sin interrupciones)...\n");
    delay_ms(200);
    init_clock();
    LOG_OK("Timer system initialized");
    print("main: Timer inicializado correctamente\n");
    delay_ms(500);

    // 3. Physical allocator (NO necesita kmalloc)
    print("main: Inicializando physical allocator...\n");
    delay_ms(200);
    physical_allocator_init();
    LOG_OK("Physical allocator ready");
    print("main: Physical allocator inicializado correctamente\n");
    delay_ms(500);

    // 4. Paginación básica (NO necesita kmalloc)
    print("main: Inicializando paginación...\n");
    delay_ms(200);
    init_paging();
    LOG_OK("Basic paging enabled");
    print("main: Paginación inicializada correctamente\n");
    delay_ms(500);

    // 5. AHORA SÍ heap allocator (puede usar physical_allocator)
    print("main: Inicializando heap allocator...\n");
    delay_ms(200);
    heap_allocator_init();
    LOG_OK("Kernel heap ready");
    print("main: Heap allocator inicializado correctamente\n");
    delay_ms(500);

    // 6. Memory system wrapper (conecta todo)
    print("main: Inicializando memory system...\n");
    delay_ms(200);
    memory_init();
    LOG_OK("Memory system unified");
    print("main: Memory system inicializado correctamente\n");
    delay_ms(500);

    // 7. On-demand paging (AHORA puede usar kmalloc)
    print("main: Inicializando on-demand paging...\n");
    delay_ms(200);
    ondemand_paging_init();
    LOG_OK("On-demand paging active");
    print("main: On-demand paging inicializado correctamente\n");
    delay_ms(500);

    // 8. VFS (Virtual File System)
    print("main: Inicializando VFS...\n");
    delay_ms(200);
    if (vfs_init() == 0)
    {
        LOG_OK("VFS initialized");
        print("main: VFS inicializado correctamente\n");

        // Test básico del VFS
        vfs_file_t *test_file;
        if (vfs_open("/test.txt", VFS_O_RDWR | VFS_O_CREAT, &test_file) == 0)
        {
            const char *data = "Hello from IR0 VFS!";
            vfs_write(test_file, data, 18);
            vfs_close(test_file);
            LOG_OK("VFS test completed");
            print("main: Test VFS completado correctamente\n");
        }
    }
    else
    {
        LOG_ERR("VFS initialization failed");
        print("main: Error inicializando VFS\n");
    }
    delay_ms(500);

    // 9. Inicializar scheduler (sin interrupciones por ahora)
    print("main: Inicializando scheduler...\n");
    delay_ms(200);
    LOG_OK("Inicializando scheduler");
    scheduler_init();
    print("main: Scheduler inicializado correctamente\n");
    delay_ms(500);

    // 10. Crear tareas de test
    print("main: Creando tareas de test...\n");
    delay_ms(200);
    LOG_OK("Creando tareas de test");
    create_test_tasks();
    print("main: Tareas de test creadas correctamente\n");
    delay_ms(500);

    // 11. SOLUCIÓN ESTABLE: NO habilitar interrupciones por ahora
    print("main: MODO ESTABLE - Interrupciones deshabilitadas\n");
    print("main: Kernel ejecutándose en modo seguro\n");
    delay_ms(1000);

    // 12. Loop infinito estable del kernel
    LOG_OK("Kernel inicializado completamente - modo estable");
    print("main: Entrando en loop infinito del kernel...\n");
    print("main: Kernel IR0 funcionando correctamente!\n");
    print("main: Presiona Ctrl+C en QEMU para salir\n");
    delay_ms(2000);

    // Loop infinito estable del kernel
    uint32_t counter = 0;
    while (1)
    {
        // Mostrar que el kernel está vivo
        if (counter % 1000000 == 0)
        {
            print("IR0 Kernel: Status: Stable :-)");
            cpu_wait();
        }
        counter++;

        // Pequeño delay para no saturar la CPU
        if (counter % 100000 == 0)
        {
            delay_ms(100);
        }
    }
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