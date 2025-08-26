#include <ir0/kernel.h>
#include <ir0/print.h>
#include <ir0/logging.h>
#include <ir0/stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <arch/common/arch_interface.h>
#include <arch/common/idt.h>
#include <drivers/timer/clock_system.h>
#include <drivers/timer/rtc/rtc.h>
#include <fs/vfs.h>
#include <fs/ir0fs.h>
#include <kernel/scheduler/scheduler.h>
#include <kernel/shell/shell.h>
#include <setup/kernel_config.h>
#include <drivers/IO/ps2.h>
#include <interrupt/arch/pic.h>
#include <drivers/storage/ata.h>
#include <interrupt/arch/keyboard.h>
#include <string.h>
#include <kernel/process/process.h>
#include <kernel/syscalls/syscalls.h>
#include <kernel/auth/auth.h>
#include <kernel/scheduler/task.h>
#include <kernel/login/login_system.h>
#include <ir0/panic/panic.h>

// Forward declarations
extern int vfs_simple_init(void);
extern void run_minix_test(void);

#ifdef __x86_64__

#include <arch/x86-64/sources/tss_x64.h>

#endif

#ifdef __x86_64__

#include <memory/paging_x64.h>

#else

#include <memory/paging_x86-32.h>

#endif

// Variables globales para debugging
volatile uint64_t *debug_ptr = (uint64_t *)0x100000;

// Global interrupt state
volatile bool interrupts_enabled = false;

// Forward declarations
static void enable_interrupts(void);
static void init_auth_system(void);
static void user_start(void);
static void start_shell(void);

// Utility function for Linux-style logging
static void log_subsystem_ok(const char *subsystem_name) {
    print_colored("[", VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    print_colored("OK", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    print_colored("] ", VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    print_colored(subsystem_name, VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    print_colored("\n", VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    // Add 1500ms delay within the function
    delay_ms(1500);
}

void main(void)
{
    // ===============================================================================
    // PROFESSIONAL KERNEL BANNER WITH DATE AND TIME
    // ===============================================================================
    
    // Initialize RTC for date/time
    rtc_init();
    
    // Get current date and time
    char date_str[16];
    char time_str[16];
    rtc_get_date_string(date_str, sizeof(date_str));
    rtc_get_time_string(time_str, sizeof(time_str));
    
    // Print professional banner
    print_colored("╔══════════════════════════════════════════════════════════════════════════════╗\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_colored("║                                                                              ║\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_colored("║                                IR0 KERNEL v0.0.0 PRE-RELEASE                               ║\n", VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    print_colored("║                                     init routine                             ║\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    print_colored("║                                                                              ║\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_colored("║            Date: ", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_colored(              date_str, VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    print_colored("         Time: ", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_colored(time_str, VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    print_colored("                 Arch: x86-64                              ║\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_colored("║                                                                              ║\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_colored("╚══════════════════════════════════════════════════════════════════════════════╝\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    
    // Brief pause for banner visibility
    delay_ms(1000);

    // ===============================================================================
    // SUBSYSTEM INITIALIZATION
    // ===============================================================================
    
    // Logging system
    extern void logging_init(void);
    logging_init();
    log_subsystem_ok("Logging system");
    
    // PS/2 Controller
    ps2_init();
    log_subsystem_ok("PS/2 controller");
    
    // Keyboard subsystem
    extern void keyboard_init(void);
    keyboard_init();
    log_subsystem_ok("Keyboard subsystem");
    
    // Interrupt controller
    pic_unmask_irq(1);
    log_subsystem_ok("Interrupt controller");
    
    // Memory management
    extern void heap_init(void);
    heap_init();
    log_subsystem_ok("Memory management");
    
    // Memory Manager initialized
    log_subsystem_ok("Memory manager");
    
    // Minix Filesystem Structure Test
    run_minix_test();
    log_subsystem_ok("Minix filesystem structure test");
    
    // Storage subsystem
    ata_init();
    log_subsystem_ok("Storage subsystem");
    
    // File systems - SOLO MINIX (con disco)
    extern int minix_fs_init(void);
    minix_fs_init();
    log_subsystem_ok("File systems (MINIX)");
    
    // Test Minix + ATA completo
    extern void run_minix_ata_test(void);
    run_minix_ata_test();
    log_subsystem_ok("Minix + ATA test");
    
    // Process management
    extern void process_init(void);
    process_init();
    log_subsystem_ok("Process management");
    
    // Clock system
    extern int clock_system_init(void);
    clock_system_init();
    log_subsystem_ok("Clock system");
    
    // Scheduler
    extern int scheduler_cascade_init(void);
    extern void scheduler_fallback_to_next(void);
    int scheduler_result = scheduler_cascade_init();
    if (scheduler_result != 0) {
        scheduler_fallback_to_next();
    }
    log_subsystem_ok("Scheduler system");
    
    // System calls
    extern void syscalls_init(void);
    syscalls_init();
    log_subsystem_ok("System calls");
    
    // ===============================================================================
    // INITIALIZE INTERRUPT SYSTEM
    // ===============================================================================
    
    // Initialize IDT
#ifdef __x86_64__
    extern void idt_init64(void);
    extern void idt_load64(void);
    idt_init64();
    idt_load64();
    extern void pic_remap64(void);
    pic_remap64();
#else
    // Inicializar IDT para 32-bit
    extern void idt_init32_simple(void);
    extern void idt_load32_simple(void);
    idt_init32_simple();
    idt_load32_simple();
    extern void pic_remap32_simple(void);
    pic_remap32_simple();
#endif
    log_subsystem_ok("Interrupt system");
    
    // ===============================================================================
    // ENABLE INTERRUPTS
    // ===============================================================================
    enable_interrupts();
    
    // ===============================================================================
    // AUTHENTICATION SYSTEM
    // ===============================================================================
    init_auth_system();
    
    // ===============================================================================
    // SHELL AND USER SPACE
    // ===============================================================================
    user_start();
    
    // ===============================================================================
    // SCHEDULER DISPATCH LOOP
    // ===============================================================================
    
    // Enter the scheduler dispatch loop - never return from here
    extern void scheduler_dispatch_loop(void);
    scheduler_dispatch_loop();
}

static void enable_interrupts(void)
{
    // Habilitar interrupciones
    __asm__ volatile("sti");
    interrupts_enabled = true;
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
        panic("Authentication system initialization failed");
    }
}

// ===============================================================================
// USER SPACE PROCESS IMPLEMENTATION
// ===============================================================================

// User space program entry point (if needed in the future)

// ===============================================================================
// SCHEDULER INTEGRATION
// ===============================================================================

// The kernel now uses the scheduler dispatch loop directly

// ===============================================================================
// USER SPACE INITIALIZATION
// ===============================================================================

// Start shell function
static void start_shell(void) {
    print_colored("╔══════════════════════════════════════════════════════════════════════════════╗\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    print_colored("║                                                                              ║\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    print_colored("║                              IR0 KERNEL SHELL                               ║\n", VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    print_colored("║                                                                              ║\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    print_colored("╚══════════════════════════════════════════════════════════════════════════════╝\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    
    print_colored("\n[IR0-SHELL] Welcome! Type 'help' for available commands.\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_colored("[IR0-SHELL] Type 'exit' to logout.\n\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    
    // Initialize shell
    shell_context_t shell_ctx;
    shell_config_t shell_config;
    strcpy(shell_config.prompt, "[IR0-SHELL] $ ");
    shell_config.max_line_length = 256;
    
    if (shell_init(&shell_ctx, &shell_config) != 0) {
        print_error("[ERROR] Failed to initialize shell\n");
        return;
    }
    
    // Start shell loop
    shell_run(&shell_ctx, &shell_config);
}

// User space initialization
static void user_start(void)
{
    // Initialize login system
    login_config_t login_config;
    login_config.correct_password = "admin";
    login_config.max_attempts = 3;
    login_config.case_sensitive = true;
    
    if (login_init(&login_config) != 0) {
        panic("Login system initialization failed");
    }
    
    // Authenticate user
    if (login_authenticate() == 0) {
        // Clear keyboard buffer to prevent residual input from being interpreted as shell commands
        extern void keyboard_buffer_clear(void);
        keyboard_buffer_clear();
        
        // Start shell after successful authentication
        start_shell();
    }
}

