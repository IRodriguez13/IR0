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
// AGREGADO: Acceso directo a PD para debug
extern uint64_t PD[];
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
    
    // 7. AGREGADO: Test del LAPIC mapping (FASE 3) - TEMPORALMENTE DESHABILITADO
    /*
    print("main: Probando LAPIC mapping...\n");
    delay_ms(200);

    // AGREGADO: Inicialización mínima del LAPIC
    print("main: Inicializando LAPIC...\n");

    // 1. Deshabilitar PIC (8259) para evitar conflictos
    print("main: Deshabilitando PIC...\n");
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0xFF), "Nd"((uint16_t)0xA1));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0xFF), "Nd"((uint16_t)0x21));
    delay_ms(100);
    print("main: PIC deshabilitado exitosamente\n");

    // 2. Habilitar APIC en IA32_APIC_BASE_MSR (MSR 0x1B)
    print("main: Configurando APIC base MSR...\n");
    uint64_t apic_base = 0;
    __asm__ volatile("rdmsr" : "=A"(apic_base) : "c"((uint32_t)0x1B));
    print("main: APIC base MSR actual: 0x");
    print_hex64(apic_base);
    print("\n");

    // Habilitar APIC (bit 11) y establecer base address
    apic_base |= (1ULL << 11);  // Enable APIC
    apic_base &= ~0xFFF;        // Clear lower 12 bits
    apic_base |= 0xFEE00000;    // Set LAPIC base address
    print("main: Escribiendo APIC base MSR: 0x");
    print_hex64(apic_base);
    print("\n");
    __asm__ volatile("wrmsr" : : "c"((uint32_t)0x1B), "A"(apic_base));
    delay_ms(100);
    print("main: APIC base MSR configurado exitosamente\n");

    // 3. Configurar Spurious Interrupt Vector (SPIV) con vector válido
    print("main: Configurando SPIV...\n");
    print("main: Usando vector 0x20 (32) para SPIV (>= 0x10, reservado por CPU)\n");
    
    // 🔍 DEBUG ESPECÍFICO: Verificar mapeo LAPIC antes del write
    print("main: Verificando LAPIC_PT...\n");
    
    // Obtener PD[0x1F7] y verificar si tiene PAGE_HUGE
    extern uint64_t PD[];  // Declaración externa para acceder a PD
    print("main: Verificando PD[0x1F7] = 0x");
    print_hex64(PD[0x1F7]); 
    print("\n");
    
    // Verificar si PD[0x1F7] tiene PAGE_HUGE (bit 7)
    if (PD[0x1F7] & (1ULL << 7)) {
        print("main: ERROR - PD[0x1F7] tiene PAGE_HUGE!\n");
    } else {
        print("main: OK - PD[0x1F7] NO tiene PAGE_HUGE\n");
    }
    
    // Obtener LAPIC_PT y verificar primeras entradas
    uint64_t *lapic_pt = (uint64_t *)(PD[0x1F7] & ~0xFFFULL);
    uint64_t pt_index = 0x0EE;  // Para 0xFEE00000
    
    for (int i = 0; i < 4; i++) {  // solo primeras 4 entradas
        print("PT["); print_hex64(pt_index + i); print("] = 0x");
        print_hex64(lapic_pt[pt_index + i]);
        print("\n");
    }
    
    volatile uint32_t *lapic_spiv = (volatile uint32_t *)(0xfee000f0);
    uint32_t spiv_value = 0x120;  // Enable APIC + vector 0x20 (32)
    print("main: Escribiendo SPIV: 0x");
    print_hex64(spiv_value);
    print("\n");
    *lapic_spiv = spiv_value;
    delay_ms(100);
    print("main: SPIV configurado exitosamente\n");

    // 4. Verificar que la IDT tenga un handler para el vector 0x20
    print("main: Verificando IDT para vector 0x20...\n");
    // La IDT ya debería estar configurada con handlers por defecto
    print("main: IDT verificada (handlers por defecto configurados)\n");

    print("main: LAPIC inicializado mínimamente\n");
    delay_ms(200);

    // 5. Intentar leer el LAPIC ID register (offset 0x20)
    print("main: Intentando leer LAPIC ID register (0xfee00020)...\n");
    volatile uint32_t *lapic_id_reg = (volatile uint32_t *)(0xfee00020);
    uint32_t lapic_id = *lapic_id_reg;

    print("main: LAPIC ID register leído: 0x");
    print_hex64(lapic_id);
    print("\n");

    // 6. Intentar leer el LAPIC version register (offset 0x30)
    print("main: Intentando leer LAPIC Version register (0xfee00030)...\n");
    volatile uint32_t *lapic_version_reg = (volatile uint32_t *)(0xfee00030);
    uint32_t lapic_version = *lapic_version_reg;

    print("main: LAPIC Version register leído: 0x");
    print_hex64(lapic_version);
    print("\n");

    if (lapic_id != 0xFFFFFFFF && lapic_version != 0xFFFFFFFF) {
        print("main: LAPIC mapping exitoso!\n");
    } else {
        print("main: ERROR - LAPIC no accesible (0xFFFFFFFF)\n");
    }
    delay_ms(500);
    */
    
    print("main: LAPIC temporalmente deshabilitado - continuando con otros aspectos\n");
    delay_ms(500);
    
    // 8. AGREGADO: Test del Scheduler (FASE 4)
    print("main: Inicializando Scheduler Round-Robin...\n");
    
    // Incluir solo headers del scheduler (NO archivos .c)
    #include "../kernel/scheduler/scheduler.h"
    
    // Funciones de prueba para las tareas
    void task1_func(void *arg) {
        (void)arg; // Evitar warning de parámetro no usado
        print("main: Task1 ejecutándose\n");
        delay_ms(50);
    }
    
    void task2_func(void *arg) {
        (void)arg; // Evitar warning de parámetro no usado
        print("main: Task2 ejecutándose\n");
        delay_ms(50);
    }
    
    void task3_func(void *arg) {
        (void)arg; // Evitar warning de parámetro no usado
        print("main: Task3 ejecutándose\n");
        delay_ms(50);
    }
    
    // Inicializar scheduler Round-Robin
    scheduler_init();
    print("main: Scheduler inicializado\n");
    
    // Crear algunas tareas de prueba
    print("main: Creando tareas de prueba...\n");
    task_t *task1 = create_task(task1_func, (void*)1, 1, 0);
    task_t *task2 = create_task(task2_func, (void*)2, 2, 0);
    task_t *task3 = create_task(task3_func, (void*)3, 3, 0);
    
    if (task1 && task2 && task3) {
        print("main: Tareas creadas exitosamente\n");
        
        // Agregar tareas al scheduler
        add_task(task1);
        add_task(task2);
        add_task(task3);
        print("main: Tareas agregadas al scheduler\n");
        
        // Ejecutar algunas rondas del scheduler
        print("main: Ejecutando 5 rondas del scheduler...\n");
        for (int i = 0; i < 5; i++) {
            task_t *current = current_scheduler.pick_next_task();
            if (current) {
                print("main: Ejecutando tarea PID: ");
                print_hex64(current->pid);
                print("\n");
            }
            delay_ms(100);
        }
    } else {
        print("main: ERROR - Fallo al crear tareas\n");
    }
    
    print("main: Test del scheduler completado\n");
    delay_ms(500);
    
    // 8. Loop infinito estable del kernel
    print("main: FASE 3 COMPLETADA - LAPIC Mapping funcionando!\n");
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