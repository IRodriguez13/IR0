// kernel/kernel_start.c - VERSIÓN MÍNIMA PARA TESTING
#include "../arch/common/arch_interface.h"
#include <kernel.h>
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
    print_colored("=== IR0 KERNEL BOOT - TESTING MÍNIMO ===\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    delay_ms(1000);

    print("main: Iniciando testing mínimo...\n");
    delay_ms(500);

    // 1. IDT primero (ANTES que cualquier cosa que pueda generar interrupts)
    print("main: Inicializando IDT...\n");
    delay_ms(200);
    idt_init();
    print("main: IDT inicializado correctamente\n");
    delay_ms(500);

    // 2. Paginación básica (solo identity mapping)
    print("main: Inicializando paginación básica...\n");
    delay_ms(200);
    init_paging();
    print("main: Paginación inicializada correctamente\n");
    delay_ms(500);

    // 3. Test básico - verificar que el kernel sigue funcionando
    print("main: Testing básico completado\n");
    print_colored("=== KERNEL FUNCIONANDO - MODO TESTING ===\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    delay_ms(1000);

    // 4. Loop infinito simple para testing
    print("main: Entrando en loop infinito para testing...\n");
    print("main: Presiona Ctrl+C en QEMU para salir\n");
    
    uint32_t counter = 0;
    while (1) {
        delay_ms(1000);
        counter++;
        print("main: Kernel funcionando - contador: ");
        print_hex64(counter);
        print("\n");
    }
}