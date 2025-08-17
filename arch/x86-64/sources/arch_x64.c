// arch/x86-64/sources/arch_x64.c - SIMPLIFICADO
#include "../../kernel/kernel_start.h"
#include "../../common/arch_interface.h"
#include "../../includes/ir0/print.h"
#include "arch_x64.h"
#include <panic/panic.h>

void kmain_x64(void)
{
    // Setup mínimo específico de x86-64
    __asm__ volatile("cli"); // Deshabilitar interrupciones al arrancar
    
    // Log de entrada
    print_colored("=== IR0 Kernel x86-64 ===\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    
    // Saltar a la función principal
    main();
    
    // Si llegamos aquí, algo salió mal
    panic("kmain_x64: main() retornó - esto no debería pasar!\n");
}
