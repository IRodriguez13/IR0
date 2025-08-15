// arch/x86-64/sources/arch_x64.c - ACTUALIZADO
#include "../../kernel/kernel_start.h"
#include "../../common/arch_interface.h"
#include "../../includes/ir0/print.h"

// Función para delay simple
static void delay_ms(uint32_t ms)
{
    // Delay simple usando loops
    for (volatile uint32_t i = 0; i < ms * 100000; i++)
    {
        __asm__ volatile("nop");
    }
}

// Esta es la función que llama el boot.asm
void kmain_x64(void)
{
    // Setup mínimo específico de x86-64 ANTES del kernel principal
    __asm__ volatile("cli"); // Deshabilitar interrupciones al arrancar.

    // Log de entrada a kmain_x64
    clear_screen();
    print_colored("=== IR0 KERNEL x86-64 BOOT ===\n", 0x0F, 0x00);
    delay_ms(1000); // Esperar 1 segundo para leer

    print("kmain_x64: Iniciando kernel...\n");
    delay_ms(500);

    // Verificar que estamos en modo 64-bit
    uint64_t cr0, cr4, efer;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    __asm__ volatile("rdmsr" : "=a"(efer) : "c"(0xC0000080));

    print("kmain_x64: CR0=");
    print_hex_compact(cr0);
    print(" CR4=");
    print_hex_compact(cr4);
    print(" EFER=");
    print_hex_compact(efer);
    print("\n");
    delay_ms(1000); // Esperar para leer los registros

    // Verificar flags importantes
    print("kmain_x64: Verificando flags...\n");
    if (cr0 & (1ULL << 31))
    {
        print("  - Paginación habilitada\n");
    }
    else
    {
        print("  - Paginación DESHABILITADA\n");
    }

    if (cr4 & (1ULL << 5))
    {
        print("  - PAE habilitado\n");
    }
    else
    {
        print("  - PAE DESHABILITADO\n");
    }

    if (efer & (1ULL << 8))
    {
        print("  - Long Mode habilitado\n");
    }
    else
    {
        print("  - Long Mode DESHABILITADO\n");
    }
    delay_ms(1000);

    // Saltamos a la función principal una vez terminado el inicio.
    print("kmain_x64: Llamando a main()...\n");
    delay_ms(500);
    main();

    // Si llegamos aquí, algo salió mal
    print_error("kmain_x64: main() retornó - esto no debería pasar!\n");
    while (1)
    {
        __asm__ volatile("hlt");
    }
}
