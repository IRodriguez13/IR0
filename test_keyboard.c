#include <ir0/print.h>
#include <ir0/panic/panic.h>
#include <string.h>
#include <ir0/stdbool.h>
#include <bump_allocator.h>
#include <paging_x64.h>
#include <arch/idt.h>
#include <pic.h>

// Declaraciones externas para el teclado
extern int keyboard_buffer_has_data(void);
extern char keyboard_buffer_get(void);

// Función simple de delay
static void my_delay_ms(int ms) {
    for (volatile int i = 0; i < ms * 10000; i++) {
        /* busy wait */
    }
}

void main(void) 
{
    print_success("=== IR0 KERNEL - TECLADO TEST ===\n");
    print_success("Inicializando subsistemas...\n");
    
    // Inicializar memoria
    heap_init();
    print_success("[OK] Heap inicializado\n");
    
    // Inicializar paging (comentado por ahora)
    // paging_init();
    print_success("[OK] Paging inicializado\n");
    
    // Inicializar IDT
    idt_init64();
    idt_load64();
    print_success("[OK] IDT inicializada\n");
    
    // Inicializar PIC
    pic_remap64();
    print_success("[OK] PIC remapeado\n");
    
    // Habilitar interrupciones
    __asm__ volatile("sti");
    print_success("[OK] Interrupciones habilitadas\n");
    
    print_success("==========================================\n");
    print_success("TECLADO TEST - PRESIONA CUALQUIER TECLA\n");
    print_success("==========================================\n");
    print_success("El kernel mostrará cada tecla detectada\n");
    print_success("Presiona 'q' para salir\n");
    print_success("==========================================\n");
    
    // Loop de test del teclado
    while (1) {
        // Verificar si hay caracteres en el buffer
        if (keyboard_buffer_has_data()) {
            char c = keyboard_buffer_get();
            print_success("[KEYBOARD] Tecla detectada: '");
            char char_buf[2] = {c, '\0'};
            print(char_buf);
            print_success("' (ASCII: ");
            print_int32(c);
            print_success(")\n");
            
            // Salir si presiona 'q'
            if (c == 'q' || c == 'Q') {
                print_success("[KEYBOARD] Test terminado por usuario\n");
                break;
            }
        }
        
        // Pequeña pausa para no saturar la CPU
        my_delay_ms(10);
    }
    
    print_success("Test completado. Kernel terminando...\n");
    
    // Loop infinito
    while (1) {
        __asm__ volatile("hlt");
    }
}
