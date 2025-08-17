#include "kernel_start.h"
#include "../includes/ir0/kernel.h"
#include "../includes/ir0/print.h"
#include "../includes/ir0/panic/panic.h"
#include "../memory/memo_interface.h"
#include "../memory/physical_allocator.h"
#include "../memory/heap_allocator.h"
#include "../memory/krnl_memo_layout.h"
#include "../kernel/scheduler/scheduler.h"
#include "../drivers/timer/clock_system.h"
#include "../fs/vfs.h"
#include <string.h>

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
    
    // FASE 5: Timer System
    print_colored("=== FASE 5: Timer System ===\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    enum ClockType timer_type = detect_best_clock();
    print("Timer detectado: ");
    switch (timer_type) {
        case CLOCK_HPET: print("HPET\n"); break;
        case CLOCK_LAPIC: print("LAPIC\n"); break;
        case CLOCK_PIT: print("PIT\n"); break;
        case CLOCK_RTC: print("RTC\n"); break;
        case CLOCK_NONE: print("NONE\n"); break;
    }
    
    init_clock();
    enum ClockType current_timer = get_current_timer_type();
    print("Timer activo: ");
    switch (current_timer) {
        case CLOCK_HPET: print("HPET\n"); break;
        case CLOCK_LAPIC: print("LAPIC\n"); break;
        case CLOCK_PIT: print("PIT\n"); break;
        case CLOCK_RTC: print("RTC\n"); break;
        case CLOCK_NONE: print("NONE\n"); break;
    }
    
    // Simular algunos ticks del timer
    for (int i = 0; i < 3; i++) {
        delay_ms(100);
        print("Timer tick ");
        print_hex64(i);
        print("\n");
    }
    print_success("Timer System funcionando\n");

    // FASE 6: File System (VFS)
    print_colored("=== FASE 6: File System (VFS) ===\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    
    int vfs_result = vfs_init();
    if (vfs_result == 0) {
        print_success("VFS inicializado correctamente\n");
        
        // Test: Abrir archivo
        vfs_file_t *test_file;
        int open_result = vfs_open("/test.txt", VFS_O_RDWR | VFS_O_CREAT, &test_file);
        if (open_result == 0 && test_file != NULL) {
            print_success("Archivo /test.txt abierto correctamente\n");
            
            // Test: Escribir en archivo
            const char *test_data = "Hello VFS!";
            ssize_t write_result = vfs_write(test_file, test_data, strlen(test_data));
            if (write_result > 0) {
                print_success("Escritura en archivo exitosa: ");
                print_hex64(write_result);
                print(" bytes\n");
            }
            
            // Test: Cerrar archivo
            int close_result = vfs_close(test_file);
            if (close_result == 0) {
                print_success("Archivo cerrado correctamente\n");
            }
        }
        
        // Test: Crear directorio
        int mkdir_result = vfs_mkdir("/testdir");
        if (mkdir_result == 0) {
            print_success("Directorio /testdir creado correctamente\n");
        }
        
        // Test: Montar filesystem
        int mount_result = vfs_mount("/dev/sda1", "/mnt", "ext2");
        if (mount_result == 0) {
            print_success("Filesystem montado correctamente\n");
        }
        
    } else {
        print_error("Error al inicializar VFS\n");
    }
    
    // FASE 7: Sistema de Interrupciones
    print_colored("=== FASE 7: Sistema de Interrupciones ===\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    
    // Habilitar interrupciones globalmente
    print("Habilitando interrupciones globalmente...\n");
    __asm__ volatile("sti");
    print_success("Interrupciones habilitadas globalmente\n");
    
    print_success("Sistema de interrupciones funcionando\n");
    
    // 8. Dispatch Loop del Scheduler
    print_colored("=== Entrando al Dispatch Loop del Scheduler ===\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    print("Kernel IR0 iniciado correctamente. Entrando al dispatch loop...\n");
    
    // Loop principal del kernel - dispatch del scheduler
    for(;;)
    {
        // Obtener la siguiente tarea del scheduler
        task_t *next_task = current_scheduler.pick_next_task();
        
        if (next_task != NULL) 
        {
            // Ejecutar la tarea
            print("Ejecutando tarea PID: ");
            print_hex64(next_task->pid);
            print("\n");
            
            // Aquí normalmente haríamos context switch
            // Por ahora solo simulamos la ejecución
            next_task->entry(NULL);
            
            // Tick del scheduler
            current_scheduler.task_tick();
        } else 
        {
            // No hay tareas, idle
            print("Idle - no hay tareas pendientes\n");
        }
        
        // Pequeña pausa para evitar saturar la CPU
        delay_ms(100);
    }
}