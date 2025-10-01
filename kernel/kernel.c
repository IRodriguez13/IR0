// kernel.c - MINIMAL kernel initialization (Linux 0.0.1 style)
#include <ir0/print.h>
#include <ir0/logging.h>
#include <ir0/panic/panic.h>
#include <string.h>
#include <drivers/IO/ps2.h>
#include <drivers/storage/ata.h>
#include <interrupt/arch/pic.h>
#include <memory/allocator.h>

// Main kernel entry point from boot
void kmain_x64(void)
{
    // CRITICAL: Initialize GDT and TSS FIRST
    extern void gdt_install(void);
    extern void setup_tss(void);
    gdt_install();
    setup_tss();
    
    // Banner
    print_colored("╔══════════════════════════════════════════════════════════════════════════════╗\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_colored("║                              IR0 KERNEL v0.0.1                               ║\n", VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    print_colored("║                              Monolithic Kernel                            ║\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_colored("║                              Arch: x86-64                                    ║\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_colored("╚══════════════════════════════════════════════════════════════════════════════╝\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);

    // Initialize subsystems with delays for visibility
    extern void logging_init(void);
    extern void delay_ms(uint32_t ms);
    
    logging_init();
    log_subsystem_ok("GDT/TSS");
    delay_ms(500);

    ps2_init();
    log_subsystem_ok("PS/2");
    delay_ms(500);

    extern void keyboard_init(void);
    keyboard_init();
    log_subsystem_ok("Keyboard");
    delay_ms(500);

    pic_unmask_irq(1);
    log_subsystem_ok("PIC");
    delay_ms(500);

    extern void simple_alloc_init(void);
    simple_alloc_init();
    log_subsystem_ok("Memory (24MB heap)");
    delay_ms(500);

    ata_init();
    log_subsystem_ok("ATA");
    delay_ms(500);

    extern int minix_fs_init(void);
    minix_fs_init();
    log_subsystem_ok("MINIX FS");
    delay_ms(500);

    extern void process_init(void);
    process_init();
    log_subsystem_ok("Process");
    delay_ms(500);

    extern int clock_system_init(void);
    clock_system_init();
    log_subsystem_ok("Clock");
    delay_ms(500);

    extern int scheduler_cascade_init(void);
    scheduler_cascade_init();
    log_subsystem_ok("CFS Scheduler");
    delay_ms(500);

    extern void syscalls_init(void);
    syscalls_init();
    log_subsystem_ok("Syscalls");
    delay_ms(500);

    // IDT
#ifdef __x86_64__
    extern void idt_init64(void);
    extern void idt_load64(void);
    idt_init64();
    idt_load64();
    pic_remap64();
#endif
    log_subsystem_ok("Interrupts");
    delay_ms(500);

    print_colored("\n╔════════════════════════════════════════════════════════════════════════════╗\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    print_colored("║                       KERNEL INITIALIZATION COMPLETE!                        ║\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    print_colored("║                    Starting shell in Ring 3 (user space)...                  ║\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    print_colored("╚══════════════════════════════════════════════════════════════════════════════╝\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    delay_ms(1000);

    // Create init process (PID 1) before going to Ring 3
    extern int start_init_process(void);
    
    print("\nCreating init process (PID 1)...\n");
    if (start_init_process() == 0) {
        print("Init process created successfully\n");
    } else {
        print("ERROR: Failed to create init process!\n");
    }
    
    // Start shell in Ring 3 (this will be called by init process)
    extern void shell_ring3_entry(void);
    extern void switch_to_user_mode(void *entry_point);
    
    print("Transitioning to Ring 3...\n");
    delay_ms(1000);
    
    print("Enabling interrupts for user space...\n");
    delay_ms(500);
    
    print("Starting init_proc_1 (PID 1)...\n");
    delay_ms(500);
    
    // Enable interrupts before switching to user mode
    __asm__ volatile("sti");
    
    extern void init_proc_1(void);
    switch_to_user_mode((void*)init_proc_1);

    // Should never return
    panic("Kernel halted unexpectedly");
    print_colored("Kernel is running in Ring 0 (supervisor mode)\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    print("Ready for user space transition when needed.\n");
    print("\n");
    
    // Idle loop - kernel stays in Ring 0
    print_colored("Kernel idle loop started. System is stable.\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    
    for (;;) {
        __asm__ volatile("hlt"); // Wait for interrupts
    }
}
