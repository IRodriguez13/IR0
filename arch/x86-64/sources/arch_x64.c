// arch/x86-64/sources/arch_x64.c - ACTUALIZADO
#include "../../kernel/kernel_start.h"
#include "../../common/arch_interface.h"
#include "../../includes/ir0/print.h"

// Esta es la función que llama el boot.asm
void kmain_x64(void) 
{
    // Setup mínimo específico de x86-64 ANTES del kernel principal
    __asm__ volatile("cli");  // Deshabilitar interrupciones al arrancar.
    
    // Log de entrada a kmain_x64
    clear_screen();
    print_colored("=== IR0 KERNEL x86-64 BOOT ===\n", 0x0F, 0x00);
    print("kmain_x64: Iniciando kernel...\n");
    
    // Saltamos a la función principal una vez terminado el inicio.
    print("kmain_x64: Llamando a main()...\n");
    main();
    
    // Si llegamos aquí, algo salió mal
    print_error("kmain_x64: main() retornó - esto no debería pasar!\n");
    while(1) {
        __asm__ volatile("hlt");
    }
}

