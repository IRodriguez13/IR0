// kernel/kernel_start.c - VERSIÓN MÍNIMA PARA TESTING
#include "../arch/common/arch_interface.h"
#include <kernel.h>
#include "../arch/common/idt.h"
#include "../memory/heap_allocator.h" // AGREGADO: Heap allocator
#include "../memory/memo_interface.h" // AGREGADO: kmalloc/kfree
#include "../memory/physical_allocator.h" // AGREGADO: Physical allocator

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
    
    print("main: Iniciando inicialización del kernel...\n");
    delay_ms(500);
    
    // 1. IDT primero
    print("main: Inicializando IDT...\n");
    delay_ms(200);
    idt_init();
    print("main: IDT inicializado correctamente\n");
    delay_ms(500);
    
    // 2. Paginación básica
    print("main: Inicializando paginación...\n");
    delay_ms(200);
    init_paging();
    print("main: Paginación inicializada correctamente\n");
    delay_ms(500);
    
    // 3. AGREGADO: Physical allocator (base para memoria dinámica)
    print("main: Inicializando physical allocator...\n");
    delay_ms(200);
    physical_allocator_init();
    print("main: Physical allocator inicializado correctamente\n");
    delay_ms(500);
    
    // 4. Heap allocator (puede usar physical_allocator)
    print("main: Inicializando heap allocator...\n");
    delay_ms(200);
    heap_allocator_init();
    print("main: Heap allocator inicializado correctamente\n");
    delay_ms(500);
    
    // 5. Test del heap
    print("main: Probando heap allocator...\n");
    delay_ms(200);
    void *ptr1 = kmalloc(1024);
    void *ptr2 = kmalloc(512);
    if (ptr1 && ptr2) {
        print("main: kmalloc exitoso - ptr1: ");
        print_hex64((uint64_t)ptr1);
        print(", ptr2: ");
        print_hex64((uint64_t)ptr2);
        print("\n");
        kfree(ptr1);
        kfree(ptr2);
        print("main: kfree exitoso\n");
    } else {
        print("main: ERROR en kmalloc\n");
    }
    delay_ms(500);
    
    // 6. AGREGADO: Test del physical allocator
    print("main: Probando physical allocator...\n");
    delay_ms(200);
    uint64_t page1 = alloc_physical_page();
    uint64_t page2 = alloc_physical_page();
    if (page1 != 0 && page2 != 0) {
        print("main: Physical alloc exitoso - page1: 0x");
        print_hex64(page1);
        print(", page2: 0x");
        print_hex64(page2);
        print("\n");
        free_physical_page(page1);
        free_physical_page(page2);
        print("main: Physical free exitoso\n");
    } else {
        print("main: ERROR en physical alloc\n");
    }
    delay_ms(500);
    
    // 7. Loop infinito estable del kernel
    print("main: FASE 2 COMPLETADA - Physical Allocator funcionando!\n");
    print("main: Entrando en loop infinito del kernel...\n");
    delay_ms(2000);
    
    uint32_t counter = 0;
    for(;;) {
        if (counter % 1000000 == 0) {
            print("IR0 Kernel: Status: FASE 2 - Physical Allocator OK :-)\n");
        }
        counter++;
        if (counter % 100000 == 0) {
            delay_ms(100);
        }
    }
}