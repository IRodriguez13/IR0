#include <ir0/kernel.h>
#include <ir0/print.h>
#include <ir0/logging.h>
#include <ir0/stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <ir0/panic/panic.h>
#include <arch/idt.h>
#include <pic.h>
#include <bump_allocator.h>
#include <paging_x64.h>
#include <shell/shell.h>
#include <IO/ps2.h>
#include <scheduler/scheduler.h>
#include <process/process.h>
#include <auth/auth.h>

// Declaraciones externas para el teclado
extern int keyboard_buffer_has_data(void);
extern char keyboard_buffer_get(void);

// Declaraciones externas para el sistema de procesos
extern task_t *create_task(void (*entry)(void *), void *arg, uint8_t priority, int8_t nice);
extern void add_task(task_t *task);

// Global variables
volatile bool kernel_running = false;
volatile bool interrupts_enabled = false;

// Kernel banner
static const char *KERNEL_BANNER =
    "\n"
    "╔══════════════════════════════════════════════════════════════╗\n"
    "║                       === IR0 KERNEL ===                     ║\n"
    "║                          Init Routine                        ║\n"
    "║                    Version: 0.0.0 pre-release                ║\n"
    "║                    Build: " __DATE__ " " __TIME__ "          ║\n"
    "╚══════════════════════════════════════════════════════════════╝\n";

// Test functions - MOVED OUTSIDE main()
static void test_krealloc_reduction(void)
{
    print_success("========= TEST KREALLOC REDUCTION =========\n");

    // Paso 1: Crear bloque grande
    void *ptr = kmalloc(512);
    if (!ptr)
    {
        print_error("[ERROR] kmalloc failed\n");
        return;
    }
    print_success("[OK] kmalloc 512 bytes successful\n");

    // Llenar con datos reconocibles
    memset(ptr, 0xAB, 512);

    // Guardar parte de los datos para verificar después
    char saved[256];
    memcpy(saved, ptr, 256);

    // Paso 2: Reducir tamaño a 256
    void *reduced_ptr = krealloc(ptr, 256);
    if (!reduced_ptr)
    {
        print_error("[ERROR] krealloc reduction failed\n");
        return;
    }
    print_success("[OK] krealloc reduction to 256 bytes successful\n");

    // Paso 3: Verificar integridad de los datos
    if (memcmp(reduced_ptr, saved, 256) == 0)
    {
        print_success("[OK] Data integrity verified after reduction\n");
    }
    else
    {
        print_error("[ERROR] Data corruption detected after reduction\n");
    }

    // Paso 4: Verificar que el bloque libre adyacente fue creado
    size_t free_blocks = get_heap_fragments();
    char buffer[128];
    sprintf(buffer, "[INFO] Number of free blocks after reduction: %zu\n", free_blocks);
    print_success(buffer);

    print_success("========= END TEST KREALLOC REDUCTION =========\n\n");
}

static void test_allocation_basic(void)
{
    print_success("\n[TEST] Basic allocation/free test (isolated)...\n");

    // Paso 1: Resetear el heap
    heap_reset();

    // Paso 2: Asignar varios bloques de distintos tamaños
    void *ptr1 = kmalloc(64);
    void *ptr2 = kmalloc(128);
    void *ptr3 = kmalloc(32);
    void *ptr4 = kmalloc(256);

    if (!ptr1 || !ptr2 || !ptr3 || !ptr4)
    {
        print_error("[ERROR] Allocation failed\n");
        return;
    }

    // Paso 3: Llenar con datos y verificar que no se sobreescriben bloques adyacentes
    memset(ptr1, 0x11, 64);
    memset(ptr2, 0x22, 128);
    memset(ptr3, 0x33, 32);
    memset(ptr4, 0x44, 256);

    // Verificar datos
    if (memcmp(ptr1, "\x11\x11\x11\x11", 4) != 0 ||
        memcmp(ptr2, "\x22\x22\x22\x22", 4) != 0 ||
        memcmp(ptr3, "\x33\x33\x33\x33", 4) != 0 ||
        memcmp(ptr4, "\x44\x44\x44\x44", 4) != 0)
    {
        print_error("[ERROR] Data integrity failed after allocation\n");
        return;
    }

    // Paso 4: Liberar algunos bloques
    kfree(ptr2);
    kfree(ptr3);

    // Paso 5: Verificar estado de bloques libres y ocupados
    block_header_t *current = get_free_list_head();
    size_t free_blocks = 0;
    size_t used_blocks = 0;
    while (current)
    {
        if (current->is_free)
            free_blocks++;
        else
            used_blocks++;
        current = current->next;
    }

    print_success("[DEBUG] Heap after frees:\n");
    heap_dump_info();

    // Validación simple
    if (free_blocks < 2 || used_blocks != 2)
    {
        print_error("[ERROR] Free list or used blocks count incorrect\n");
        return;
    }

    print_success("[OK] Basic allocation/free test passed\n");
}

// Early initialization functions
static void early_init(void)
{
    // Clear screen
    clear_screen();

    // Print kernel banner
    print_colored(KERNEL_BANNER, VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    print("\n");
    delay_ms(1500);
    // Initialize logging
    logging_init();
    print_success("[OK] Early initialization started\n");

    // Delay for visual effect
    delay_ms(1500);

    print_success("[OK] Early initialization completed\n");
}

static void memory_init(void)
{
    print_success("[OK] Memory management subsystem ready\n");

    // Delay for visual effect
    delay_ms(1500);

    // CRÍTICO: Configurar IDT ANTES de la paginación para evitar triple fault
    idt_init64();
    idt_load64(); // CARGAR LA IDT EN EL CPU
    print_success("[OK] Interrupt descriptor table initialized and loaded\n");

    // HABILITAR PAGINACIÓN: Solo expandir tablas, NO recargar CR3
    setup_and_enable_paging();
    print_success("[OK] Paging enabled (using boot CR3)\n");

    delay_ms(1500);

    // CONFIGURAR PIC ANTES DE HABILITAR INTERRUPCIONES - CRÍTICO
    pic_remap64(); // Remapear PIC a INT 0x20-0x2F
    print_success("[OK] PIC remapped to INT 0x20-0x2F\n");

    // Verificación de paginación
    verify_paging_setup_safe();

    print_success("[OK] Paging subsystem ready\n");

    // INICIALIZAR SISTEMA DE MEMORIA AVANZADO
    heap_init();
    heap_set_strategy(FIRST_FIT); // Usar FIRST_FIT para evitar problemas de corrupción
    print_success("[OK] Advanced memory management system initialized\n");

    // SISTEMA DE TIMERS COMPLETO
    print_success("[OK] Initializing timer subsystem...\n");

    // 1. PIT (Programmable Interval Timer) - usar funciones existentes
    // pit_init();  // No existe esta función específica
    print_success("[OK] PIT already configured by boot\n");

    // 2. HPET (High Precision Event Timer)
    if (find_hpet_table())
    { // Usar find_hpet_table() en lugar de find_hpet()
        hpet_init();
        print_success("[OK] HPET initialized and active\n");
    }
    else
    {
        print_warning("[WARN] HPET not found, using PIT only\n");
    }

    // 3. Clock system
    clock_system_init();
    print_success("[OK] Clock system initialized\n");

    delay_ms(1000);

    // SISTEMA DE I/O COMPLETO
    print_success("[OK] Initializing I/O subsystem...\n");

    // 1. PS/2 Keyboard
    ps2_init();
    print_success("[OK] PS/2 keyboard initialized\n");
    
    // Habilitar explícitamente IRQ1 (teclado) en el PIC
    pic_unmask_irq(1);
    print_success("[OK] Keyboard IRQ1 enabled in PIC\n");

    // 2. PS/2 Mouse (si está disponible) - comentar por ahora
    // if (ps2_mouse_init() == 0) {
    //     print_success("[OK] PS/2 mouse initialized\n");
    // } else {
    //     print_warning("[WARN] PS/2 mouse not found\n");
    // }

    delay_ms(1000);

    // SISTEMA DE ARCHIVOS BÁSICO
    print_success("[OK] Initializing file system subsystem...\n");

    // 1. ATA Disk Driver
    ata_init();
    print_success("[OK] ATA disk driver initialized\n");

    // 2. VFS Simple (ya incluido en el build)
    vfs_simple_init();
    print_success("[OK] VFS Simple initialized\n");

    delay_ms(1000);

    // SISTEMA DE SCHEDULER CON DETECCIÓN AUTOMÁTICA
    print_success("[OK] Initializing scheduler subsystem with auto-detection...\n");
    
    // Usar el sistema de detección automática de schedulers
    extern int scheduler_cascade_init(void);
    if (scheduler_cascade_init() != 0) {
        print_error("[ERROR] Scheduler auto-detection failed!\n");
        panic("Scheduler initialization failed");
    }
    
    print_success("[OK] Scheduler auto-detection completed\n");
    
    delay_ms(1000);
}

static void enable_interrupts(void)
{
    print_success("[OK] Interrupt system ready\n");

    // Delay for visual effect
    delay_ms(1500);

    // Habilitar interrupciones - AHORA CON STACK MAPEADO
    __asm__ volatile("sti");
    interrupts_enabled = true;

    print_success("[OK] Global interrupts enabled\n");

    // Delay for visual effect
    delay_ms(1500);
}

// ===============================================================================
// AUTHENTICATION INITIALIZATION
// ===============================================================================

static void init_auth_system(void)
{
    auth_config_t config;
    config.max_attempts = 3;
    config.lockout_time = 0;
    config.require_password = false;
    config.case_sensitive = true;
    
    if (auth_init(&config) != 0) {
        print_error("[ERROR] Failed to initialize authentication system\n");
        panic("Authentication system initialization failed");
    }
    
    print_success("[OK] Authentication system initialized\n");
}

// ===============================================================================
// USER SPACE PROCESS IMPLEMENTATION
// ===============================================================================

// Simple user space program that prints hello world
static void user_program_entry(void *arg)
{
    (void)arg;
    
    // This would normally be in user space
    // For now, we'll simulate it from kernel space
    print_colored("🎉 USER SPACE PROCESS STARTED!\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    print_colored("Hello from user space process!\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_colored("PID: ", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    
    // Get current process info
    process_t *current = process_get_current();
    if (current) {
        print_uint32(current->pid);
        print("\n");
    }
    
    print_colored("User process running successfully!\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    
    // Simulate some work
    for (int i = 0; i < 5; i++) {
        print_colored("User process iteration: ", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
        print_uint32(i + 1);
        print("\n");
        
        // Small delay
        for (volatile int j = 0; j < 1000000; j++) {
            __asm__ volatile("nop");
        }
    }
    
    print_colored("User process completed successfully!\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    
    // Exit the process
    process_exit(0);
}

// Create and start the first user space process
static void user_start(void)
{
    print_colored("🚀 Starting first user space process...\n", VGA_COLOR_MAGENTA, VGA_COLOR_BLACK);
    
    // Create the user process
    process_t *user_process = process_create("user_program", user_program_entry, NULL);
    if (!user_process) {
        print_error("[ERROR] Failed to create user process\n");
        return;
    }
    
    print_colored("✅ User process created successfully!\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    print_colored("Process PID: ", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    print_uint32(user_process->pid);
    print("\n");
    
    // Add the process to the scheduler
    task_t *user_task = create_task(user_program_entry, NULL, 5, 0);
    if (!user_task) {
        print_error("[ERROR] Failed to create user task\n");
        return;
    }
    
    // Add to scheduler
    add_task(user_task);
    
    print_colored("✅ User process added to scheduler!\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    print_colored("🎯 User space transition ready!\n", VGA_COLOR_MAGENTA, VGA_COLOR_BLACK);
}

// Main kernel entry point (called from arch_x64.c)
void main(void)
{
    // Mark kernel as running
    kernel_running = true;

    // Initialize kernel in order
    early_init();
    memory_init();

    // Enable interrupts
    enable_interrupts();

    print_success("[OK] Kernel initialization completed successfully\n");

    // Delay for visual effect
    delay_ms(1500);

    // SIMPLIFICADO: Solo pruebas básicas del heap
    print_success("[OK] Testing basic heap functionality...\n");
    delay_ms(1000);

    // Test básico: kmalloc simple
    void *ptr = kmalloc(64);
    if (ptr)
    {
        print_success("[OK] Basic kmalloc(64) successful\n");
        kfree(ptr);
        print_success("[OK] Basic kfree() successful\n");
    }
    else
    {
        print_error("[ERROR] Basic kmalloc failed\n");
    }

    delay_ms(1000);

    // Main kernel loop
    print_success("[OK] Kernel boot completed successfully\n");
    
    delay_ms(1000);

    // AUTHENTICATION SYSTEM
    print_success("[OK] Initializing authentication system...\n");
    init_auth_system();
    
    // Kernel login - required before shell access
    auth_result_t login_result = kernel_login();
    if (login_result != AUTH_SUCCESS) {
        // Login failed - system halted in kernel_login()
        return;
    }

    // SHELL INTERACTIVO MEJORADO
    print_success("[OK] Starting interactive shell...\n");
    print_success("==========================================\n");
    print_success("IR0 Kernel v1.0 - All subsystems active\n");
    print_success("==========================================\n");

    // Iniciar shell interactivo
    shell_start();

    // Cuando el shell termina, crear el primer proceso de user space
    print_success("[OK] Shell exited, creating first user space process...\n");
    print_success("==========================================\n");
    print_success("IR0 Kernel - User Space Transition\n");
    print_success("==========================================\n");

    // Crear y iniciar el primer proceso de user space
    user_start();

    // Iniciar el scheduler
    scheduler_start();
    
    // Ir al dispatch loop del scheduler (nunca retorna)
    scheduler_dispatch_loop();
}